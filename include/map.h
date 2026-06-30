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

// 譜面摘要，供選單顯示（不載入完整音符序列）
struct BeatmapInfo {
    std::string title;
    std::string artist;
    std::string version;  // 難度名
    int mode = -1;        // osu 模式：0=std 1=taiko 2=catch 3=mania
    int keyCount = 0;
    int noteCount = 0;
    int lengthMs = 0;  // 最後一個音符時間
};

// 譜面檔頭（只含篩選所需欄位）
struct BeatmapHeader {
    int mode = -1;
    int keyCount = 0;
    bool isMania7K() const { return mode == 3 && keyCount == 7; }
};

// 極輕量探測：只讀到 [HitObjects] 前就停，用於大量譜面庫的快速篩選
BeatmapHeader probeBeatmap(const std::filesystem::path& filename);

// 讀取整張 osu!mania 7K 譜面（音訊檔名、標題、音符）
Beatmap loadBeatmap(const std::filesystem::path& filename);

// 輕量解析：只取摘要欄位並統計音符數/長度，不建立音符向量
BeatmapInfo loadBeatmapInfo(const std::filesystem::path& filename);

// 僅取得音符序列（沿用舊介面，內部呼叫 loadBeatmap）
std::vector<ManiaNote> parse7K(const std::filesystem::path& filename);

#endif
