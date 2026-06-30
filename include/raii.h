#ifndef RAII_H
#define RAII_H

#include <raylib.h>

// 只對「擁有資源、需要手動釋放」的 raylib 型別包薄 RAII。
// 繪圖／輸入／計時等無狀態函式仍直接呼叫原生 API。
// 所有包裝皆 move-only：資源不可複製，避免重複釋放。

// 視窗 + 音訊裝置的生命週期 guard，放在 main 最外層
class RaylibApp {
public:
    RaylibApp(int width, int height, const char* title, int targetFps = 144) {
        InitWindow(width, height, title);
        InitAudioDevice();
        SetTargetFPS(targetFps);
    }
    ~RaylibApp() {
        if (IsAudioDeviceReady()) CloseAudioDevice();
        CloseWindow();
    }
    RaylibApp(const RaylibApp&) = delete;
    RaylibApp& operator=(const RaylibApp&) = delete;
};

// 串流音樂（背景曲）；載入失敗時 valid() 為 false，呼叫端可選擇靜音降級
class MusicRes {
public:
    explicit MusicRes(const char* path) : music_(LoadMusicStream(path)) {}
    ~MusicRes() {
        if (valid()) UnloadMusicStream(music_);
    }
    MusicRes(MusicRes&& other) noexcept : music_(other.music_) { other.music_ = {}; }
    MusicRes& operator=(MusicRes&&) = delete;
    MusicRes(const MusicRes&) = delete;

    bool valid() const { return music_.stream.buffer != nullptr; }
    Music& get() { return music_; }

private:
    Music music_{};
};

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

#endif
