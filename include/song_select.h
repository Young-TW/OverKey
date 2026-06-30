#ifndef SONG_SELECT_H
#define SONG_SELECT_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// 掃描 maps/ 目錄並提供選歌畫面。假設視窗已由外層開啟。
class SongSelect {
public:
    explicit SongSelect(std::filesystem::path mapsDir);

    // 跑選單迴圈，回傳玩家選的譜面路徑；nullopt 代表玩家要離開程式。
    std::optional<std::filesystem::path> run();

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
