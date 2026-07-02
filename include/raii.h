#ifndef RAII_H
#define RAII_H

// 無頭 TUI 用 raylib 相容層取代 raylib（免 OpenGL/X11）；GUI 仍用真正的 raylib。
#ifdef OVERKEY_HEADLESS
#include "rl_compat.h"
#else
#include <raylib.h>
#endif

// 只對「擁有資源、需要手動釋放」的 raylib 型別包薄 RAII。
// 繪圖／輸入／計時等無狀態函式仍直接呼叫原生 API。
// 所有包裝皆 move-only：資源不可複製，避免重複釋放。

#ifndef OVERKEY_HEADLESS  // 視窗/貼圖：僅 GUI（需 raylib 圖形層）

// 視窗 + 音訊裝置的生命週期 guard，放在 main 最外層
class RaylibApp {
public:
    RaylibApp(int width, int height, const char* title, int targetFps = 240) {
        // VSync：與螢幕刷新率同步，消除撕裂/judder（在低刷新率螢幕上感覺更順）
        SetConfigFlags(FLAG_VSYNC_HINT);
        InitWindow(width, height, title);
        InitAudioDevice();
        SetTargetFPS(targetFps);  // VSync 未生效時的保險上限
    }
    ~RaylibApp() {
        if (IsAudioDeviceReady()) CloseAudioDevice();
        CloseWindow();
    }
    RaylibApp(const RaylibApp&) = delete;
    RaylibApp& operator=(const RaylibApp&) = delete;
};

#endif  // !OVERKEY_HEADLESS

// 串流音樂（背景曲）；載入失敗時 valid() 為 false，呼叫端可選擇靜音降級
class MusicRes {
public:
    explicit MusicRes(const char* path) : music_(LoadMusicStream(path)) {}
    explicit MusicRes(Music m) : music_(m) {}  // 接管已載入的 Music（可於背景執行緒載入）
    ~MusicRes() {
        if (valid()) UnloadMusicStream(music_);
    }
    MusicRes(MusicRes&& other) noexcept : music_(other.music_) { other.music_ = {}; }
    MusicRes& operator=(MusicRes&&) = delete;
    MusicRes(const MusicRes&) = delete;

    bool valid() const { return music_.stream.buffer != nullptr; }
    Music& get() { return music_; }
    Music release() {  // 交出所有權，析構時不再卸載（卸載可移到其他執行緒）
        Music m = music_;
        music_ = {};
        return m;
    }

private:
    Music music_{};
};

#ifndef OVERKEY_HEADLESS  // 貼圖：僅 GUI

// 貼圖資源（之後做美術時用；目前切片以幾何圖形渲染，先備著）
class TextureRes {
public:
    explicit TextureRes(const char* path) : tex_(LoadTexture(path)) {}
    ~TextureRes() {
        if (tex_.id != 0) UnloadTexture(tex_);
    }
    TextureRes(TextureRes&& other) noexcept : tex_(other.tex_) { other.tex_.id = 0; }
    TextureRes& operator=(TextureRes&&) = delete;
    TextureRes(const TextureRes&) = delete;

    bool valid() const { return tex_.id != 0; }
    const Texture2D& get() const { return tex_; }
    operator const Texture2D&() const { return tex_; }

private:
    Texture2D tex_{};
};

#endif  // !OVERKEY_HEADLESS

// 短音效。可由檔案載入，或接管程式合成的 Sound（如 LoadSoundFromWave 的結果）。
class SoundRes {
public:
    explicit SoundRes(const char* path) : snd_(LoadSound(path)) {}
    explicit SoundRes(Sound s) : snd_(s) {}  // 接管已建立的 Sound
    ~SoundRes() {
        if (valid()) UnloadSound(snd_);
    }
    SoundRes(SoundRes&& other) noexcept : snd_(other.snd_) { other.snd_ = {}; }
    SoundRes& operator=(SoundRes&&) = delete;
    SoundRes(const SoundRes&) = delete;

    bool valid() const { return snd_.stream.buffer != nullptr; }
    Sound& get() { return snd_; }

private:
    Sound snd_{};
};

#endif
