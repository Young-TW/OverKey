#ifndef GAME_H
#define GAME_H

#include <array>
#include <filesystem>
#include <vector>

#include "map.h"

// 一局下落式音遊的狀態與主迴圈。
// 只負責遊戲邏輯；資源生命週期交給 raii.h 的包裝。
class Game {
public:
    Game(Beatmap map, std::filesystem::path audioPath);
    void run();

private:
    enum class Phase { Playing, Result };
    enum class NoteState { Idle, Holding, Done };  // Holding 僅用於長押
    enum class Judgment { None, Perfect, Good, Miss };

    void update(double songTimeMs);
    void judgePress(int column, double songTimeMs);
    void judgeRelease(int column, double songTimeMs);
    void addJudgment(Judgment j);
    Judgment judgeByError(double absErrMs) const;

    void draw(double songTimeMs) const;
    void drawPlayfield(double songTimeMs) const;
    void drawResult() const;

    double accuracy() const;     // 0..100
    const char* grade() const;

    Beatmap map_;
    std::filesystem::path audioPath_;

    std::vector<NoteState> state_;        // 與 map_.notes 對齊
    std::array<int, 7> holding_{};        // 各軌道正在按住的長押 index，-1 = 無

    Phase phase_ = Phase::Playing;
    double songEndMs_ = 0.0;              // 最後一個音符時間 + 收尾餘量

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
