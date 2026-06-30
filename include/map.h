#ifndef MAP_H
#define MAP_H

#include <string>
#include <filesystem>
#include <vector>

struct ManiaNote {
    int column;  // 音軌 (0-6)
    int startTime;
    int endTime; // 若為長押則有值，否則為 -1
};

struct Beatmap {
    std::string title;          // [Metadata] Title
    std::string audioFilename;  // [General] AudioFilename（相對於 .osu 所在目錄）
    std::vector<ManiaNote> notes;
};

// 讀取整張 osu!mania 7K 譜面（音訊檔名、標題、音符）
Beatmap loadBeatmap(const std::filesystem::path& filename);

// 僅取得音符序列（沿用舊介面，內部呼叫 loadBeatmap）
std::vector<ManiaNote> parse7K(const std::filesystem::path& filename);

#endif
