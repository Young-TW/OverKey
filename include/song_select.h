#ifndef SONG_SELECT_H
#define SONG_SELECT_H

#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "library.h"
#include "map.h"
#include "map_import.h"
#include "metric_cache.h"
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
    // cache 由呼叫端持有（壽命須長於本物件）：套用已算好的指標，排序狀態寫回由它持久化
    SongSelect(std::filesystem::path mapsDir, MenuCache& cache);
    ~SongSelect();  // 停止背景指標計算執行緒並存回快取

    // 跑選單迴圈：Enter→Play、Tab→Settings、Esc/關窗→Quit。
    // musicVolume 用於 hover 副歌試聽；scores 用於詳情面板顯示最佳成績與等第排序。
    MenuResult run(Viewport& vp, float musicVolume, const ScoreBook& scores,
                   bool& autoPlay, float& rate);

private:
    struct Entry {
        std::filesystem::path path;
        std::string label;
        MapMetrics metrics;  // 延遲載入並快取（摘要／難度）
        int keyCount = 0;    // 掃描時探測得知（鍵數篩選用；解析免費）
    };
    // 顯示用的一列：一般項目或群組標頭（entry = entries_ 索引，-1 表標頭）
    struct Row {
        int entry;
        std::string header;  // 僅標頭列有效
        int itemPos = 0;     // 項目列在全部項目中的次序（計數器顯示用）
        bool isHeader() const { return entry < 0; }
    };

    void draw() const;
    void clampScroll();
    void stepSelection(int dir);      // 移到上/下一個「項目」列（跳過標頭）
    void ensureSelectedInfo();  // 確保目前選取項的摘要已載入
    bool makeEntry(const std::filesystem::path& p, Entry& out) const;  // 探測+標籤，不支援回傳 false
    void ingestNewMaps();       // 撈背景匯入器新解出的譜面，插進清單（保留目前選取）
    void rebuildView(const std::filesystem::path& preserve);  // 依排序鍵 + query_ 重建列（含群組標頭）
    std::filesystem::path selectedPath() const;  // 目前選取項的路徑（無符合時回空）

    // 背景指標計算（長度／音符／難度排序用；結果由主執行緒 drain 併入 entries_）
    void startMetricLoad();
    void stopMetricLoad();
    bool drainMetricResults();  // 回傳是否有新結果併入

    std::filesystem::path mapsDir_;
    MenuCache& cache_;      // 選單狀態／排序指標的磁碟快取（外部持有）
    MapImporter importer_;  // 背景解壓 .osz/.qp
    std::vector<Entry> entries_;
    std::vector<Row> rows_;          // 排序 + 篩選 + 分組後的顯示列
    int itemCount_ = 0;              // rows_ 中的項目列數（計數器用）
    const ScoreBook* scores_ = nullptr;  // 詳情面板與等第排序用（run 期間有效）
    int selected_ = 0;               // rows_ 中的位置（保證停在項目列上）
    int scroll_ = 0;
    std::string query_;              // 搜尋關鍵字（即時子字串篩選清單）
    bool searching_ = false;         // 搜尋輸入模式（/ 進入，Esc 清除離開）
    SortKey sortKey_ = SortKey::Title;  // 目前排序／分組鍵（S 切換；建構時由快取還原）
    bool sortAsc_ = true;               // 排序方向（Shift+S 反轉）
    int keyFilter_ = kKeyFilterAll;     // 鍵數篩選 bitmask（數字鍵 4–7 / 點晶片切換）
    bool autoPlay_ = false;  // 供 draw() 顯示指示器
    float rate_ = 1.0f;

    // 背景指標計算狀態
    std::thread metricWorker_;
    std::atomic<bool> metricCancel_{false};
    std::atomic<bool> metricRunning_{false};
    std::atomic<int> metricProgress_{0};
    int metricTotal_ = 0;  // 0 → 允許啟動新一輪（僅主執行緒讀寫）
    std::mutex metricM_;
    std::deque<std::pair<std::filesystem::path, MapMetrics>> metricDone_;
};

#endif
