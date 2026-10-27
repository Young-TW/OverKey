#ifndef RAII_H
#define RAII_H

// 無頭 TUI 用 raylib 相容層（miniaudio + stb）取代 raylib，免 OpenGL/X11。
#include "rl_compat.h"

// 只對「擁有資源、需要手動釋放」的 raylib API 型別包薄 RAII。
// 所有包裝皆 move-only：資源不可複製，避免重複釋放。

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
