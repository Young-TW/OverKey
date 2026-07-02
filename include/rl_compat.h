#ifndef RL_COMPAT_H
#define RL_COMPAT_H

// raylib 相容層（僅供無頭 TUI 使用）。
// TUI 只需要 raylib 的「音訊」與「影像」子集，卻得整包連結 raylib（含 GLFW/OpenGL/X11）
// 才能編譯——在無圖形環境（如叢集）會因缺 OpenGL 而無法建置。
// 這裡用 miniaudio（音訊）+ stb_image/stb_image_write/stb_image_resize2 + stb_vorbis（影像/ogg）
// 重新實作 TUI 用到的那組 API，型別與函式簽名刻意對齊 raylib，讓 tui_main.cpp 幾乎免改。
// 皆為純 CPU 實作，不需視窗、OpenGL 或 X11。

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// raylib TraceLog 等級；TUI 只用到 LOG_NONE，值不重要。
enum { LOG_ALL = 0, LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL, LOG_NONE };

struct Color {
    unsigned char r, g, b, a;
};

// 一律以 RGBA8 表示（format/mipmaps 僅為相容欄位）。
struct Image {
    void* data;
    int width;
    int height;
    int mipmaps;
    int format;
};

struct Wave {
    unsigned int frameCount;
    unsigned int sampleRate;
    unsigned int sampleSize;  // 位元數（本專案固定 16）
    unsigned int channels;
    void* data;
};

// 只保留 TUI 會碰的欄位：buffer 作為「是否有效／是否持有資源」的哨兵。
struct AudioStream {
    void* buffer;
};

struct Music {
    AudioStream stream;
};

struct Sound {
    AudioStream stream;
};

// ---- 音訊裝置 ----
void InitAudioDevice(void);
void CloseAudioDevice(void);
bool IsAudioDeviceReady(void);
void SetTraceLogLevel(int logLevel);  // no-op，僅為相容

// ---- 串流音樂 ----
Music LoadMusicStream(const char* fileName);
void UnloadMusicStream(Music music);
void PlayMusicStream(Music music);
void StopMusicStream(Music music);    // 停止並倒回開頭（與 raylib 一致）
void PauseMusicStream(Music music);   // 暫停，保留位置
void ResumeMusicStream(Music music);
void UpdateMusicStream(Music music);  // no-op：miniaudio 引擎自有音訊執行緒
void SeekMusicStream(Music music, float position);  // 秒
void SetMusicVolume(Music music, float volume);
void SetMusicPitch(Music music, float pitch);
float GetMusicTimePlayed(Music music);
float GetMusicTimeLength(Music music);

// ---- 短音效 ----
Sound LoadSound(const char* fileName);
Sound LoadSoundFromWave(Wave wave);
void UnloadSound(Sound sound);
void PlaySound(Sound sound);  // 從頭重播單一聲道
void SetSoundVolume(Sound sound, float volume);
void UnloadWave(Wave wave);

// ---- 影像（純 CPU，供 kitty 圖片協定使用）----
Image LoadImage(const char* fileName);
Image GenImageColor(int width, int height, Color color);
void ImageDrawCircle(Image* dst, int centerX, int centerY, int radius, Color color);
void ImageResize(Image* image, int newWidth, int newHeight);
unsigned char* ExportImageToMemory(Image image, const char* fileType, int* fileSize);
void UnloadImage(Image image);
void MemFree(void* ptr);

#endif  // RL_COMPAT_H
