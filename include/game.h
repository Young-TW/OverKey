#ifndef GAME_H
#define GAME_H

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
    void update(double songTimeMs);
    void draw(double songTimeMs) const;
    void judgePress(int column, double songTimeMs);

    Beatmap map_;
    std::filesystem::path audioPath_;
    std::vector<bool> judged_;  // 與 map_.notes 對齊：該音符是否已判定

    int score_ = 0;
    int combo_ = 0;
    int maxCombo_ = 0;
    int perfects_ = 0;
    int goods_ = 0;
    int misses_ = 0;

    enum class Judgment { None, Perfect, Good, Miss };
    Judgment lastJudge_ = Judgment::None;
};

#endif
