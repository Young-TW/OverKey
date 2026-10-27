#ifndef METRIC_CACHE_H
#define METRIC_CACHE_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "library.h"

// 選單狀態 + 排序指標的磁碟快取（overkey-cache.txt）：排序鍵／方向、最後選中的譜面，
// 與每張譜面已算好的摘要（BeatmapInfo）和難度（QSS／osu★）。難度要整張解析 + QSS，
// 啟動重算很貴，這裡以 mtime + size 驗證快取，檔案變動才重算。純邏輯、無圖形/音訊依賴。
//
// 檔案格式（tab 分隔，path 壓最後可含 tab 以外的任意字元）：
//   sort \t key \t asc
//   sel \t path
//   m \t mtime \t size \t flags \t mode \t keyCount \t lengthMs \t noteCount \t
//     previewTimeMs \t quaverDiff \t osuStar \t title \t artist \t version \t
//     audio \t background \t path
// flags：bit0=有摘要、bit1=有難度；文字欄位寫入時 tab/換行以空白取代。
class MenuCache {
public:
    struct Item {
        std::int64_t mtime = 0;  // last_write_time().time_since_epoch().count()
        std::uint64_t size = 0;
        bool hasInfo = false;
        bool hasDiff = false;
        BeatmapInfo info{};
        double quaverDiff = 0.0;
        double osuStar = 0.0;
    };

    explicit MenuCache(std::filesystem::path file);  // 建構時載入；無檔 → 空快取

    int sortKey() const { return sortKey_; }  // 已夾限在 SortKey 範圍
    bool sortAsc() const { return sortAsc_; }
    void setSort(int key, bool asc);  // 夾限 key；有變更才標 dirty

    // 最後選中的譜面路徑（空＝未記錄，啟動時回退第一首）；有變更才標 dirty
    const std::string& selectedPath() const { return selectedPath_; }
    void setSelected(const std::string& path);

    // mtime/size 與檔案現況相符才把快取填入 m 缺漏欄位（已齊不覆寫）；回傳是否有填入
    bool apply(const std::filesystem::path& path, MapMetrics& m) const;
    // 記錄 path 現況（mtime/size）與 m 已具備的指標；檔案變動過會先清掉舊欄位再合併
    void store(const std::filesystem::path& path, const MapMetrics& m);

    bool dirty() const { return dirty_; }
    void save();  // 有變更才整檔重寫（dirty 旗標重置）

private:
    std::filesystem::path file_;
    std::map<std::string, Item> items_;  // key = path.string()（與 ScoreBook 同做法）
    std::string selectedPath_;  // 最後選中的譜面（path 是穩定鍵，背景匯入重排後仍找得回）
    int sortKey_ = 0;
    bool sortAsc_ = true;
    bool dirty_ = false;
};

#endif
