#ifndef GAME_H
#define GAME_H

#include <array>
#include <deque>
#include <filesystem>

#include "map.h"
#include "play.h"
#include "pp.h"
#include "scores.h"
#include "settings.h"

class Viewport;

// raylib 前端：驅動 PlaySession（純邏輯 core），負責渲染、輸入、音訊。
class Game {
public:
    Game(Beatmap map, std::filesystem::path audioPath, Settings settings,
         ScoreRecord prevBest = {}, bool autoPlay = false, float rate = 1.0f);
    void run(Viewport& vp);

    // 遊玩中以 F3/F4 調整後的下落速度，供呼叫端存回設定
    float scrollSpeed() const { return settings_.scrollSpeed; }
    bool autoPlay() const { return autoPlay_; }

    bool completed() const { return phase_ == Phase::Result; }  // 是否打完（reach 結算）
    int finalScore() const { return session_.score(); }
    double finalAccuracy() const { return session_.accuracy(); }
    const char* finalGrade() const { return session_.grade(); }
    int finalMaxCombo() const { return session_.maxCombo(); }

private:
    enum class Phase { Playing, Paused, Result };

    void drawPlayfield(double songTimeMs) const;
    void drawResult() const;
    void drawPauseOverlay() const;
    void drawErrorMeter() const;  // 右側垂直命中誤差條（osu 風）
    void triggerFlash(int lane, Judgment j);

    Beatmap map_;
    std::filesystem::path audioPath_;
    Settings settings_;
    ScoreRecord prevBest_;  // 進入前的最佳成績（結算顯示用）
    PlaySession session_;
    bool autoPlay_ = false;
    float rate_ = 1.0f;

    int keyCount_ = 7;          // 音軌數（4 或 7）
    const int* laneKeys_ = nullptr;  // 對應鍵位集（指向 settings_.keys / keys4）
    int playfieldW_ = 0;
    int originX_ = 0;

    double offsetMs_ = 0.0;    // = settings_.audioOffsetMs
    double approachMs_ = 0.0;  // 下落時間，由 scrollSpeed 決定
    double pxPerMs_ = 0.0;     // 下落速度

    Phase phase_ = Phase::Playing;
    int pauseSel_ = 0;  // 暫停選單選項：0 Resume / 1 Retry / 2 Quit

    // 遊戲中即時 pp / Quaver rating 計數器：整場難度（quaverDiff_，結算用）
    // 每局只算一次；即時值對「從頭到此刻的譜面前綴」重跑 QSS（節流 kLiveIntervalSec），
    // 見 pp.h estimateLiveRatings。打完時（前綴=整張圖）即結算畫面顯示的數字。
    static constexpr double kLiveIntervalSec = 0.25;  // 前綴 QSS 重算節流
    double quaverDiff_ = 0.0;
    RatingEstimate liveRating_{};
    double liveCalcAt_ = -1.0e9;  // 上次重算的牆鐘時間（GetTime()）

    // 命中回饋（用牆鐘計時，與遊戲邏輯時鐘分開）；上限 7 軌
    std::array<double, 7> laneFlash_{};
    std::array<Judgment, 7> laneFlashJudge_{};

    // 命中誤差條（右側垂直）：最近命中的有號誤差與牆鐘時間戳，畫漸淡刻度用
    struct ErrMark {
        double errMs;  // >0 = 偏晚按
        Judgment j;
        double t;
    };
    std::deque<ErrMark> errMarks_;
};

#endif
