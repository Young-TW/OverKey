#ifndef LIBRARY_H
#define LIBRARY_H

#include <filesystem>
#include <optional>
#include <string>

#include "map.h"
#include "scores.h"

// 選歌清單的排序 / 分組（群組標頭）邏輯：純邏輯、無 raylib 依賴，GUI 與 TUI 共用。
//
// 運作方式：
//  - 前端持有每張譜面的 MapMetrics（延遲載入、快取於 Entry 內）。
//  - Title / Grade 排序不需載入檔案（label 與 ScoreBook 即可）；Length / Notes 需要
//    BeatmapInfo；Difficulty 需要整張解析 + QSS（最重）。前端於背景執行緒呼叫
//    loadMetrics 補齊缺漏的指標。
//  - 排序用 sortRowLess（缺資料者一律墊底，不隨方向改變）；分組標頭用 groupName，
//    相鄰同名視為同組。

// 排序鍵（循環順序固定；nextSortKey 依此繞行）
enum class SortKey { Title, Difficulty, Grade, Length, Notes };

const char* sortKeyName(SortKey k);  // 顯示用名稱（"TITLE"、"DIFFICULTY"、...）
SortKey nextSortKey(SortKey k);

// 該排序鍵是否分別需要 BeatmapInfo / 難度（前端據此決定要不要排程載入）
bool sortNeedsInfo(SortKey k);
bool sortNeedsDiff(SortKey k);

// 每張譜面延遲載入的排序 / 分組指標
struct MapMetrics {
    std::optional<BeatmapInfo> info;  // 摘要（長度／音符數／鍵數）
    std::optional<double> quaverDiff; // QSS 難度
    std::optional<double> osuStar;    // 對應 osu!mania 星級
};

// 載入單張指標（只補缺的）。withDiff=true 會整張解析 + QSS（較重，背景執行緒用）。
void loadMetrics(const std::filesystem::path& path, MapMetrics& m, bool withDiff);

// 排序的一列：指標皆指向外部擁有的資料（排序期間須保持有效）。best 可為 nullptr。
struct SortRow {
    const std::string* label;
    const MapMetrics* metrics;
    const ScoreRecord* best;
};

// 等第排序值：無紀錄 0，D=1 ... SS=6。best 為 nullptr 或 !valid 皆視為無紀錄。
int gradeRank(const ScoreRecord* best);

// 嚴格弱序：canonical（Title A→Z、難度易→難、等第佳→差、長度短→長、音符少→多），
// asc=false 反轉；缺資料者一律墊底（不受方向影響）；同值以 label 不區分大小寫破平局。
bool sortRowLess(SortKey k, bool asc, const SortRow& a, const SortRow& b);

// 群組標頭名稱：同排序鍵下相鄰同名視同組（如 "SS"、"2 - 4 ★"、"UNPLAYED"、"#"）。
std::string groupName(SortKey k, const SortRow& row);

// ---- 鍵數篩選器（4K–7K 各一個開關，bitmask；GUI 與 TUI 選單共用）----
// bit k 代表 kK 顯示與否；kKeyFilterMin..kKeyFilterMax 以外不屬支援範圍。
inline constexpr int kKeyFilterMin = 4;
inline constexpr int kKeyFilterMax = 7;
inline constexpr int kKeyFilterAll =  // 預設全開＝不篩
    ((1 << (kKeyFilterMax - kKeyFilterMin + 1)) - 1) << kKeyFilterMin;

// 譜面鍵數是否通過篩選（支援範圍外恆 false）
bool keyFilterAllows(int mask, int keyCount);
// 切換單一鍵數的顯示；為避免清單被篩空，唯一仍開著的鍵數不可關（回傳原 mask）
int toggleKeyFilter(int mask, int keyCount);

#endif
