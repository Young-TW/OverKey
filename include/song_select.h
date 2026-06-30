#ifndef SONG_SELECT_H
#define SONG_SELECT_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "map.h"
#include "scores.h"

class Viewport;

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
    // musicVolume 用於 hover 副歌試聽；scores 用於詳情面板顯示最佳成績。
    MenuResult run(Viewport& vp, float musicVolume, const ScoreBook& scores);

private:
    struct Entry {
        std::filesystem::path path;
        std::string label;
        std::optional<BeatmapInfo> info;  // 延遲載入並快取
    };

    void draw() const;
    void clampScroll();
    void ensureSelectedInfo();  // 確保目前選取項的摘要已載入

    std::filesystem::path mapsDir_;
    std::vector<Entry> entries_;
    const ScoreBook* scores_ = nullptr;  // 詳情面板顯示最佳成績用（run 期間有效）
    int selected_ = 0;
    int scroll_ = 0;
};

#endif
