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
    int keyCount = 7;           // 音軌數（[Difficulty] CircleSize）
    std::vector<ManiaNote> notes;
};

// 譜面摘要，供選單顯示（不載入完整音符序列）
struct BeatmapInfo {
    std::string title;
    std::string artist;
    std::string version;        // 難度名
    std::string audioFilename;       // [General] AudioFilename
    std::string backgroundFilename;  // 背景圖（osu [Events] / Quaver BackgroundFile）
    int mode = -1;                   // osu 模式：0=std 1=taiko 2=catch 3=mania
    int keyCount = 0;
    int noteCount = 0;
    int lengthMs = 0;        // 最後一個音符時間
    int previewTimeMs = -1;  // [General] PreviewTime（試聽起點，副歌）；-1=未指定
};

// 譜面檔頭（篩選 + 清單標籤所需欄位，只讀到 [HitObjects] 前）
struct BeatmapHeader {
    int mode = -1;
    int keyCount = 0;
    std::string title;
    std::string artist;
    std::string version;
    // 目前支援的 mania 鍵數：4K 與 7K
    bool isSupported() const { return mode == 3 && (keyCount == 4 || keyCount == 7); }
    // 清單顯示名："Artist - Title [Diff]"；缺 title 時回傳空字串
    std::string label() const {
        if (title.empty()) return "";
        std::string s;
        if (!artist.empty()) s += artist + " - ";
        s += title;
        if (!version.empty()) s += " [" + version + "]";
        return s;
    }
};

// 極輕量探測：只讀到 [HitObjects] 前就停，用於大量譜面庫的快速篩選
BeatmapHeader probeBeatmap(const std::filesystem::path& filename);

// 讀取整張 osu!mania 7K 譜面（音訊檔名、標題、音符）
Beatmap loadBeatmap(const std::filesystem::path& filename);

// 輕量解析：只取摘要欄位並統計音符數/長度，不建立音符向量
BeatmapInfo loadBeatmapInfo(const std::filesystem::path& filename);

// 在譜面資料夾尋找主打擊取樣（normal/soft/drum-hitnormal）；找不到回傳空路徑
std::filesystem::path findHitSound(const std::filesystem::path& mapDir);

// 僅取得音符序列（沿用舊介面，內部呼叫 loadBeatmap）
std::vector<ManiaNote> parse7K(const std::filesystem::path& filename);

#endif
