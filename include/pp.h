#ifndef PP_H
#define PP_H

// osu!mania pp 與 Quaver rating 估算（純邏輯，無 raylib 依賴，GUI / TUI 共用）。
//
// 演算法來源（皆為官方開源實作的忠實移植）：
//  - Quaver 難度：Quaver.API「QSS」DifficultyProcessorKeys v0.0.5（伺服器/客戶端同款，
//    含其已知的 7K 兩次運算與 jack 寫入 roll 欄位等上游行為，bug-for-bug 相容）。
//  - Quaver rating：RatingProcessorKeys → rating = difficulty × (accuracy/98)^6。
//  - osu!mania pp：ppy/osu ManiaPerformanceCalculator（scoreV2 年代的 lazer 公式）：
//      acc  = (320·n320 + 300·n300 + 200·n100 ... )/(320·N)
//      pp   = 8·max(SR−0.15, 0.05)^2.2 · max(0, 5·acc−4) · (1 + 0.1·min(1, N/1500))
//
// OverKey → 各遊戲的判定對應（依各遊戲實際判定窗對齊；刻意不使用各遊戲的最高級，
// 即 osu 320g（16–22ms）與 Quaver MARV（±18ms）視為打不到，故估值略偏保守下限）：
//   OverKey PERFECT(±35) → osu 300 / Quaver PERF(±43)
//   OverKey GREAT  (±65) → osu 200 / Quaver GREAT(±76)
//   OverKey GOOD   (±95) → osu 100 / Quaver GOOD(±106)
//   OverKey BAD    (±125)→ osu 50  / Quaver OKAY(±127)
//   OverKey MISS         → osu MISS / Quaver MISS

#include <vector>

#include "map.h"

class PlaySession;

// Quaver 官方難度（QSS v0.0.5），rate 為播放速率（1.0 原速）。
// notes 為 0-based column、ms 時間；keyCount 4 或 7。回傳值即 Quaver 譜面 rating 數字。
double quaverDifficulty(const std::vector<ManiaNote>& notes, int keyCount, float rate = 1.0f);

// Quaver 官方 accuracy（0..100）：Marv 100 / Perf 98.25 / Great 65 / Good 25 / Okay -100 / Miss -50
double quaverAccuracy(int nMarv, int nPerf, int nGreat, int nGood, int nOkay, int nMiss);

// Quaver 官方 rating：difficulty × (acc/98)^6
double quaverRating(double difficulty, double accuracy);

// osu!mania pp（lazer 公式）。totalHits 同 osu 算法：長押頭尾各計一次。
double osuManiaPP(double starRating, int n320, int n300, int n200, int n100, int n50, int nMiss);

// 估算用：osu!mania 自訂 accuracy（0..1），與 pp 公式內部相同
double osuManiaCustomAccuracy(int n320, int n300, int n200, int n100, int n50, int nMiss);

// 由 Quaver 難度推估 osu!mania star rating（線性啟發式，SR ≈ quaverDiff / 4.4；
// ranked 轉譜群體上手校的粗略值，個別譜面依風格可能有明顯偏差）
double osuEstimateStarRating(double quaverDifficulty);

// 從 PlaySession 的判定統計推估兩款遊戲的成績。
// quaverDiff 為 quaverDifficulty() 預先算好的結果（每局只需算一次）。
struct RatingEstimate {
    double quaverDiff = 0.0;    // QSS v0.0.5 難度（含 rate）
    double quaverAcc = 0.0;     // 0..100
    double quaverRating = 0.0;  // = quaverDiff × (quaverAcc/98)^6
    double osuStar = 0.0;       // ≈ quaverDiff / 4.4（啟發式）
    double osuPP = 0.0;         // lazer mania pp
};
RatingEstimate estimateRatings(double quaverDiff, const PlaySession& session);

#endif
