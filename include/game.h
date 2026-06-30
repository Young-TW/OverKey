#ifndef GAME_H
#define GAME_H

#include <array>
#include <filesystem>
#include <vector>

#include "map.h"
#include "settings.h"

// 一局下落式音遊的狀態與主迴圈。
// 只負責遊戲邏輯；資源生命週期交給 raii.h 的包裝。
class Game {
public:
    Game(Beatmap map, std::filesystem::path audioPath, Settings settings);
    void run();

private:
    enum class Phase { Playing, Result };
    enum class NoteState { Idle, Holding, Done };  // Holding 僅用於長押
    enum class Judgment { None, Perfect, Good, Miss };

    void update(double songTimeMs);
    void judgePress(int column, double songTimeMs);
    void judgeRelease(int column, double songTimeMs);
    void addJudgment(Judgment j);
    void triggerFlash(int column, Judgment j);  // 命中聲光回饋
    Judgment judgeByError(double absErrMs) const;

    void draw(double songTimeMs) const;
    void drawPlayfield(double songTimeMs) const;
    void drawResult() const;

    double accuracy() const;     // 0..100
    const char* grade() const;

    Beatmap map_;
    std::filesystem::path audioPath_;
    Settings settings_;

    double offsetMs_ = 0.0;               // = settings_.audioOffsetMs
    double approachMs_ = 0.0;             // 下落時間，由 scrollSpeed 決定
    double pxPerMs_ = 0.0;                // 下落速度

    std::vector<NoteState> state_;        // 與 map_.notes 對齊
    std::array<int, 7> holding_{};        // 各軌道正在按住的長押 index，-1 = 無

    Phase phase_ = Phase::Playing;
    double songEndMs_ = 0.0;              // 最後一個音符時間 + 收尾餘量

    double signedErrSum_ = 0.0;           // 命中音符的有號誤差和，用於校正建議
    int errSamples_ = 0;

    // 命中回饋（用牆鐘計時，與遊戲邏輯時鐘分開）
    std::array<double, 7> laneFlash_{};        // 各軌道最近一次命中的 GetTime()
    std::array<Judgment, 7> laneFlashJudge_{}; // 該次命中的判定（決定顏色）
    bool hitSoundQueued_ = false;              // 本幀是否要播放打擊聲

    int score_ = 0;
    int combo_ = 0;
    int maxCombo_ = 0;
    int perfects_ = 0;
    int goods_ = 0;
    int misses_ = 0;
    int totalUnits_ = 0;                  // 判定單位總數（普通=1，長押=2）
    long long pointsAccum_ = 0;           // 已得分點數，用於 accuracy

    Judgment lastJudge_ = Judgment::None;
};

#endif
