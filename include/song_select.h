#ifndef SONG_SELECT_H
#define SONG_SELECT_H

#include <filesystem>
#include <string>
#include <vector>

// 選單一次互動的結果
enum class MenuAction { Play, Settings, Quit };
struct MenuResult {
    MenuAction action;
    std::filesystem::path path;  // 僅 Play 時有效
};

// 掃描 maps/ 目錄並提供選歌畫面。假設視窗已由外層開啟。
class SongSelect {
public:
    explicit SongSelect(std::filesystem::path mapsDir);

    // 跑選單迴圈：Enter→Play、Tab→Settings、Esc/關窗→Quit。
    MenuResult run();

private:
    struct Entry {
        std::filesystem::path path;
        std::string label;
    };

    void draw() const;
    void clampScroll();

    std::filesystem::path mapsDir_;
    std::vector<Entry> entries_;
    int selected_ = 0;
    int scroll_ = 0;
};

#endif
