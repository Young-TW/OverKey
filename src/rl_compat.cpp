// rl_compat.h 的實作：以 miniaudio + stb 提供 TUI 需要的 raylib 音訊/影像子集。
// 設計取捨：音樂與音效都「一次全解碼成 PCM」再交給 miniaudio 引擎播放，而非串流。
//  - 好處：seek 變成 O(1)（直接跳 PCM frame），徹底避開 MP3 無 seek table、
//    從頭掃描解碼造成的卡頓（原本得靠背景執行緒閃避）。
//  - 代價：整首歌的 PCM 佔記憶體（約 s16 立體聲 3 分鐘 ≈ 30MB），對桌機/叢集皆可接受。

#include "rl_compat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "miniaudio.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

// stb_vorbis：僅取用這個入口（實作在 vendor/impl_stb_vorbis.c）。
extern "C" int stb_vorbis_decode_filename(const char* filename, int* channels,
                                          int* sample_rate, short** output);

namespace {

ma_engine g_engine;
bool g_engineReady = false;

// 一段已解碼、可播放的音訊：PCM 由本物件持有，audio_buffer 參照它、sound 掛在引擎上。
struct Clip {
    std::vector<short> pcm;  // 交錯 s16
    ma_uint32 sampleRate = 0;
    ma_uint32 channels = 0;
    ma_uint64 frameCount = 0;
    ma_audio_buffer buffer{};
    ma_sound sound{};
    bool bufferOk = false;
    bool soundOk = false;
};

std::string lowerExt(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

// 用 miniaudio 內建解碼器（wav/flac/mp3）把整檔讀成交錯 s16。
bool decodeWithMiniaudio(const char* path, std::vector<short>& out, ma_uint32& sr,
                         ma_uint32& ch) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 0, 0);  // 0=沿用原生
    ma_decoder dec;
    if (ma_decoder_init_file(path, &cfg, &dec) != MA_SUCCESS) return false;
    sr = dec.outputSampleRate;
    ch = dec.outputChannels;

    out.clear();
    constexpr ma_uint64 kChunk = 8192;
    std::vector<short> tmp(kChunk * ch);
    for (;;) {
        ma_uint64 got = 0;
        if (ma_decoder_read_pcm_frames(&dec, tmp.data(), kChunk, &got) != MA_SUCCESS && got == 0)
            break;
        if (got == 0) break;
        out.insert(out.end(), tmp.begin(), tmp.begin() + got * ch);
        if (got < kChunk) break;
    }
    ma_decoder_uninit(&dec);
    return !out.empty();
}

// Ogg/Vorbis 交給 stb_vorbis 一次解碼。
bool decodeWithVorbis(const char* path, std::vector<short>& out, ma_uint32& sr, ma_uint32& ch) {
    int channels = 0, sampleRate = 0;
    short* data = nullptr;
    const int frames = stb_vorbis_decode_filename(path, &channels, &sampleRate, &data);
    if (frames <= 0 || data == nullptr) {
        std::free(data);
        return false;
    }
    sr = static_cast<ma_uint32>(sampleRate);
    ch = static_cast<ma_uint32>(channels);
    out.assign(data, data + static_cast<size_t>(frames) * channels);
    std::free(data);
    return true;
}

// 由已解碼 PCM 建立可播放 Clip（掛上引擎）。失敗回 nullptr。
Clip* makeClip(std::vector<short>&& pcm, ma_uint32 sr, ma_uint32 ch) {
    if (!g_engineReady || pcm.empty() || ch == 0) return nullptr;
    Clip* c = new Clip();
    c->pcm = std::move(pcm);
    c->sampleRate = sr;
    c->channels = ch;
    c->frameCount = c->pcm.size() / ch;

    ma_audio_buffer_config bcfg =
        ma_audio_buffer_config_init(ma_format_s16, ch, c->frameCount, c->pcm.data(), nullptr);
    bcfg.sampleRate = sr;
    if (ma_audio_buffer_init(&bcfg, &c->buffer) != MA_SUCCESS) {
        delete c;
        return nullptr;
    }
    c->bufferOk = true;

    if (ma_sound_init_from_data_source(&g_engine, &c->buffer, 0, nullptr, &c->sound) !=
        MA_SUCCESS) {
        ma_audio_buffer_uninit(&c->buffer);
        delete c;
        return nullptr;
    }
    c->soundOk = true;
    return c;
}

Clip* loadClip(const char* path) {
    std::vector<short> pcm;
    ma_uint32 sr = 0, ch = 0;
    const std::string ext = lowerExt(path);
    const bool ok = (ext == "ogg") ? decodeWithVorbis(path, pcm, sr, ch)
                                   : decodeWithMiniaudio(path, pcm, sr, ch);
    if (!ok) return nullptr;
    return makeClip(std::move(pcm), sr, ch);
}

void destroyClip(Clip* c) {
    if (!c) return;
    if (c->soundOk) ma_sound_uninit(&c->sound);
    if (c->bufferOk) ma_audio_buffer_uninit(&c->buffer);
    delete c;
}

Clip* clipOf(void* buffer) { return static_cast<Clip*>(buffer); }

}  // namespace

// ==== 音訊裝置 ====
void InitAudioDevice(void) {
    if (g_engineReady) return;
    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &g_engine) == MA_SUCCESS) g_engineReady = true;
}

void CloseAudioDevice(void) {
    if (!g_engineReady) return;
    ma_engine_uninit(&g_engine);
    g_engineReady = false;
}

bool IsAudioDeviceReady(void) { return g_engineReady; }

void SetTraceLogLevel(int) {}

// ==== 串流音樂 ====
Music LoadMusicStream(const char* fileName) {
    Music m{};
    m.stream.buffer = loadClip(fileName);
    return m;
}

void UnloadMusicStream(Music music) { destroyClip(clipOf(music.stream.buffer)); }

void PlayMusicStream(Music music) {
    if (Clip* c = clipOf(music.stream.buffer)) ma_sound_start(&c->sound);
}

void StopMusicStream(Music music) {
    if (Clip* c = clipOf(music.stream.buffer)) {
        ma_sound_stop(&c->sound);
        ma_sound_seek_to_pcm_frame(&c->sound, 0);  // raylib 的 Stop 會倒回開頭
    }
}

void PauseMusicStream(Music music) {
    if (Clip* c = clipOf(music.stream.buffer)) ma_sound_stop(&c->sound);
}

void ResumeMusicStream(Music music) {
    if (Clip* c = clipOf(music.stream.buffer)) ma_sound_start(&c->sound);
}

void UpdateMusicStream(Music) {}  // 引擎自有執行緒，免手動補緩衝

void SeekMusicStream(Music music, float position) {
    if (Clip* c = clipOf(music.stream.buffer)) {
        const ma_uint64 frame = static_cast<ma_uint64>(std::max(0.0f, position) * c->sampleRate);
        ma_sound_seek_to_pcm_frame(&c->sound, frame);
    }
}

void SetMusicVolume(Music music, float volume) {
    if (Clip* c = clipOf(music.stream.buffer)) ma_sound_set_volume(&c->sound, volume);
}

void SetMusicPitch(Music music, float pitch) {
    if (Clip* c = clipOf(music.stream.buffer)) ma_sound_set_pitch(&c->sound, pitch);
}

float GetMusicTimePlayed(Music music) {
    Clip* c = clipOf(music.stream.buffer);
    if (!c) return 0.0f;
    float cursor = 0.0f;
    ma_sound_get_cursor_in_seconds(&c->sound, &cursor);
    return cursor;
}

float GetMusicTimeLength(Music music) {
    Clip* c = clipOf(music.stream.buffer);
    if (!c || c->sampleRate == 0) return 0.0f;
    return static_cast<float>(static_cast<double>(c->frameCount) / c->sampleRate);
}

// ==== 短音效 ====
Sound LoadSound(const char* fileName) {
    Sound s{};
    s.stream.buffer = loadClip(fileName);
    return s;
}

Sound LoadSoundFromWave(Wave wave) {
    Sound s{};
    if (wave.data == nullptr || wave.channels == 0) return s;
    const short* src = static_cast<const short*>(wave.data);
    std::vector<short> pcm(src, src + static_cast<size_t>(wave.frameCount) * wave.channels);
    s.stream.buffer = makeClip(std::move(pcm), wave.sampleRate, wave.channels);
    return s;
}

void UnloadSound(Sound sound) { destroyClip(clipOf(sound.stream.buffer)); }

void PlaySound(Sound sound) {
    if (Clip* c = clipOf(sound.stream.buffer)) {
        ma_sound_seek_to_pcm_frame(&c->sound, 0);  // 每次從頭觸發（單聲道，與 raylib 預設一致）
        ma_sound_start(&c->sound);
    }
}

void SetSoundVolume(Sound sound, float volume) {
    if (Clip* c = clipOf(sound.stream.buffer)) ma_sound_set_volume(&c->sound, volume);
}

void UnloadWave(Wave wave) { std::free(wave.data); }

// ==== 影像（純 CPU）====
namespace {
constexpr int kRGBA = 4;

inline void blendPixel(unsigned char* px, Color color) {
    const float a = color.a / 255.0f;
    px[0] = static_cast<unsigned char>(color.r * a + px[0] * (1.0f - a));
    px[1] = static_cast<unsigned char>(color.g * a + px[1] * (1.0f - a));
    px[2] = static_cast<unsigned char>(color.b * a + px[2] * (1.0f - a));
    px[3] = static_cast<unsigned char>(color.a + px[3] * (1.0f - a));
}
}  // namespace

Image LoadImage(const char* fileName) {
    Image im{};
    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(fileName, &w, &h, &comp, kRGBA);  // 一律轉 RGBA
    if (!data) return im;  // data == nullptr 代表載入失敗
    im.data = data;
    im.width = w;
    im.height = h;
    im.mipmaps = 1;
    im.format = kRGBA;
    return im;
}

Image GenImageColor(int width, int height, Color color) {
    Image im{};
    if (width <= 0 || height <= 0) return im;
    auto* data = static_cast<unsigned char*>(
        std::malloc(static_cast<size_t>(width) * height * kRGBA));
    if (!data) return im;
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        data[i * kRGBA + 0] = color.r;
        data[i * kRGBA + 1] = color.g;
        data[i * kRGBA + 2] = color.b;
        data[i * kRGBA + 3] = color.a;
    }
    im.data = data;
    im.width = width;
    im.height = height;
    im.mipmaps = 1;
    im.format = kRGBA;
    return im;
}

void ImageDrawCircle(Image* dst, int centerX, int centerY, int radius, Color color) {
    if (!dst || !dst->data || radius <= 0) return;
    auto* px = static_cast<unsigned char*>(dst->data);
    const int r2 = radius * radius;
    const int x0 = std::max(0, centerX - radius), x1 = std::min(dst->width - 1, centerX + radius);
    const int y0 = std::max(0, centerY - radius), y1 = std::min(dst->height - 1, centerY + radius);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - centerX, dy = y - centerY;
            if (dx * dx + dy * dy <= r2)
                blendPixel(px + (static_cast<size_t>(y) * dst->width + x) * kRGBA, color);
        }
    }
}

void ImageResize(Image* image, int newWidth, int newHeight) {
    if (!image || !image->data || newWidth <= 0 || newHeight <= 0) return;
    unsigned char* out = stbir_resize_uint8_srgb(
        static_cast<const unsigned char*>(image->data), image->width, image->height, 0, nullptr,
        newWidth, newHeight, 0, STBIR_RGBA);
    if (!out) return;
    std::free(image->data);
    image->data = out;
    image->width = newWidth;
    image->height = newHeight;
}

unsigned char* ExportImageToMemory(Image image, const char* fileType, int* fileSize) {
    if (fileSize) *fileSize = 0;
    if (!image.data || image.width <= 0 || image.height <= 0) return nullptr;
    (void)fileType;  // TUI 只匯出 PNG

    std::vector<unsigned char> buf;
    auto writer = [](void* ctx, void* data, int len) {
        auto* v = static_cast<std::vector<unsigned char>*>(ctx);
        auto* p = static_cast<unsigned char*>(data);
        v->insert(v->end(), p, p + len);
    };
    if (!stbi_write_png_to_func(writer, &buf, image.width, image.height, kRGBA, image.data,
                                image.width * kRGBA) ||
        buf.empty())
        return nullptr;

    auto* out = static_cast<unsigned char*>(std::malloc(buf.size()));
    if (!out) return nullptr;
    std::memcpy(out, buf.data(), buf.size());
    if (fileSize) *fileSize = static_cast<int>(buf.size());
    return out;
}

void UnloadImage(Image image) { std::free(image.data); }

void MemFree(void* ptr) { std::free(ptr); }
