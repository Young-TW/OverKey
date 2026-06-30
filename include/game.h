#ifndef GAME_H
#define GAME_H

#include <array>
#include <filesystem>
#include <vector>

#include "map.h"
#include "settings.h"

class Viewport;

// 一局下落式音遊的狀態與主迴圈。
// 只負責遊戲邏輯；資源生命週期交給 raii.h 的包裝。
class Game {
public:
    Game(Beatmap map, std::filesystem::path audioPath, Settings settings);
    void run(Viewport& vp);

    enum class Phase { Playing, Result };
    enum class NoteState { Idle, Holding, Done };  // Holding 僅用於長押
    enum class Judgment { None, Perfect, Great, Good, Bad, Miss };

private:
    static constexpr int kJudgeCount = 6;   // Judgment 列舉數量
    static constexpr int kHistBins = 41;    // 誤差直方圖格數

    void update(double songTimeMs);
    void judgePress(int column, double songTimeMs);
    void judgeRelease(int column, double songTimeMs);
    void addJudgment(Judgment j);
    void triggerFlash(int column, Judgment j);
    void recordError(double signedErrMs);   // 累計校正用誤差與直方圖
    Judgment judgeByError(double absErrMs) const;

    void drawPlayfield(double songTimeMs) const;
    void drawResult() const;

    double accuracy() const;  // 0..100
    const char* grade() const;

    Beatmap map_;
    std::filesystem::path audioPath_;
    Settings settings_;

    double offsetMs_ = 0.0;    // = settings_.audioOffsetMs
    double approachMs_ = 0.0;  // 下落時間，由 scrollSpeed 決定
    double pxPerMs_ = 0.0;     // 下落速度

    std::vector<NoteState> state_;  // 與 map_.notes 對齊
    std::array<int, 7> holding_{};  // 各軌道正在按住的長押 index，-1 = 無

    Phase phase_ = Phase::Playing;
    double songEndMs_ = 0.0;  // 最後一個音符時間 + 收尾餘量

    int score_ = 0;
    int combo_ = 0;
    int maxCombo_ = 0;
    int totalUnits_ = 0;        // 判定單位總數（普通=1，長押=2）
    long long pointsAccum_ = 0; // 已得分點數，用於 accuracy
    std::array<int, kJudgeCount> counts_{};  // 各判定等級次數

    double signedErrSum_ = 0.0;              // 有號誤差和（>0=偏晚），校正建議用
    int errSamples_ = 0;
    std::array<int, kHistBins> hist_{};      // 誤差直方圖

    // 命中回饋（用牆鐘計時，與遊戲邏輯時鐘分開）
    std::array<double, 7> laneFlash_{};
    std::array<Judgment, 7> laneFlashJudge_{};
    bool hitSoundQueued_ = false;

    Judgment lastJudge_ = Judgment::None;
};

#endif
