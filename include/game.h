#ifndef GAME_H
#define GAME_H

#include <array>
#include <filesystem>

#include "map.h"
#include "play.h"
#include "settings.h"

class Viewport;

// raylib 前端：驅動 PlaySession（純邏輯 core），負責渲染、輸入、音訊。
class Game {
public:
    Game(Beatmap map, std::filesystem::path audioPath, Settings settings);
    void run(Viewport& vp);

    // 遊玩中以 F3/F4 調整後的下落速度，供呼叫端存回設定
    float scrollSpeed() const { return settings_.scrollSpeed; }

private:
    enum class Phase { Playing, Result };

    void drawPlayfield(double songTimeMs) const;
    void drawResult() const;
    void triggerFlash(int lane, Judgment j);

    Beatmap map_;
    std::filesystem::path audioPath_;
    Settings settings_;
    PlaySession session_;

    int keyCount_ = 7;          // 音軌數（4 或 7）
    const int* laneKeys_ = nullptr;  // 對應鍵位集（指向 settings_.keys / keys4）
    int playfieldW_ = 0;
    int originX_ = 0;

    double offsetMs_ = 0.0;    // = settings_.audioOffsetMs
    double approachMs_ = 0.0;  // 下落時間，由 scrollSpeed 決定
    double pxPerMs_ = 0.0;     // 下落速度

    Phase phase_ = Phase::Playing;

    // 命中回饋（用牆鐘計時，與遊戲邏輯時鐘分開）；上限 7 軌
    std::array<double, 7> laneFlash_{};
    std::array<Judgment, 7> laneFlashJudge_{};
};

#endif
