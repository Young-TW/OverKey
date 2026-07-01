#ifndef PLAY_H
#define PLAY_H

#include <array>
#include <cstddef>
#include <vector>

#include "map.h"

// 判定等級與音符狀態：前端無關，GUI / TUI 共用。
enum class Judgment { None, Perfect, Great, Good, Bad, Miss };
enum class NoteState { Idle, Holding, Done };  // Holding 僅用於長押

const char* judgeName(Judgment j);

// 平滑歌曲時鐘：每幀以 frame delta 前進，音訊播放位置僅作緩慢校正，
// 避免直接讀取量化的音訊位置造成下落頓挫。完全不碰渲染/音訊 API。
class SongClock {
public:
    explicit SongClock(double leadInMs) : timeMs_(-leadInMs) {}

    // frameDeltaMs：本幀經過的毫秒；audioPosMs：音訊播放位置(ms)，<0 表示無音訊。
    void tick(double frameDeltaMs, double audioPosMs);

    double timeMs() const { return timeMs_; }
    bool running() const { return started_; }          // 已過開場倒數
    bool startedThisTick() const { return startedThisTick_; }  // 本幀剛跨過 0

    // 直接跳到指定時間（跳過前奏用）；視為已開始
    void seek(double ms) {
        timeMs_ = ms;
        started_ = true;
        startedThisTick_ = false;
    }

private:
    double timeMs_;
    bool started_ = false;
    bool startedThisTick_ = false;
};

// 一局遊玩的純邏輯：音符狀態、判定、計分、統計。
// 只吃「時間」與「按鍵事件」，不碰渲染/輸入/音訊；命中以事件回報給前端做聲光。
class PlaySession {
public:
    static constexpr int kCols = 7;
    static constexpr int kHistBins = 41;

    struct HitEvent {
        int lane;
        Judgment judgment;  // 僅命中（非 Miss）才回報
    };

    explicit PlaySession(std::vector<ManiaNote> notes);

    void press(int lane, double songTimeMs);
    void release(int lane, double songTimeMs);
    void advance(double songTimeMs);  // 處理被動 Miss 與長押自動完成

    std::vector<HitEvent> drainEvents();  // 取出並清空本批命中事件

    // ---- 查詢（供前端渲染 / 結算）----
    const std::vector<ManiaNote>& notes() const { return notes_; }
    NoteState stateOf(std::size_t i) const { return state_[i]; }
    int score() const { return score_; }
    int combo() const { return combo_; }
    int maxCombo() const { return maxCombo_; }
    int count(Judgment j) const { return counts_[static_cast<int>(j)]; }
    Judgment lastJudgment() const { return lastJudge_; }
    double accuracy() const;  // 0..100
    const char* grade() const;
    const std::array<int, kHistBins>& histogram() const { return hist_; }
    int errSamples() const { return errSamples_; }
    double meanErrorMs() const { return errSamples_ ? signedErrSum_ / errSamples_ : 0.0; }
    double songEndMs() const { return songEndMs_; }
    bool finished(double songTimeMs) const { return songTimeMs >= songEndMs_; }
    int firstNoteMs() const { return firstNoteMs_; }  // 第一個音符時間，無音符為 -1

private:
    void addJudgment(Judgment j);
    void recordError(double signedErrMs);
    Judgment judgeByError(double absErrMs) const;

    std::vector<ManiaNote> notes_;
    std::vector<NoteState> state_;
    std::array<int, kCols> holding_;  // 各軌道正在按住的長押 index，-1 = 無
    std::vector<HitEvent> events_;

    int score_ = 0;
    int combo_ = 0;
    int maxCombo_ = 0;
    int totalUnits_ = 0;
    long long pointsAccum_ = 0;
    std::array<int, 6> counts_{};  // 對應 Judgment 列舉

    double signedErrSum_ = 0.0;
    int errSamples_ = 0;
    std::array<int, kHistBins> hist_{};

    double songEndMs_ = 0.0;
    int firstNoteMs_ = -1;
    Judgment lastJudge_ = Judgment::None;
};

#endif
