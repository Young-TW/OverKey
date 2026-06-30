#include <algorithm>
#include <cmath>
#include <utility>

#include "play.h"

namespace {

// 五級判定：依 |誤差| 由嚴到鬆，超過最後一級即 Miss
constexpr double kWindowMs[] = {35.0, 65.0, 95.0, 125.0};  // Perfect/Great/Good/Bad
constexpr Judgment kTiers[] = {Judgment::Perfect, Judgment::Great, Judgment::Good,
                               Judgment::Bad};
constexpr double kHitWindowMs = 125.0;                       // 最大判定視窗
constexpr int kPoints[] = {0, 300, 200, 100, 50, 0};        // 對應 Judgment 列舉
constexpr double kEndPadMs = 1500.0;                        // 最後音符後到結算的餘量

}  // namespace

const char* judgeName(Judgment j) {
    switch (j) {
        case Judgment::Perfect: return "PERFECT";
        case Judgment::Great:   return "GREAT";
        case Judgment::Good:    return "GOOD";
        case Judgment::Bad:     return "BAD";
        case Judgment::Miss:    return "MISS";
        default:                return "";
    }
}

void SongClock::tick(double frameDeltaMs, double audioPosMs) {
    startedThisTick_ = false;
    if (!started_) {
        timeMs_ += frameDeltaMs;  // 開場倒數（由負數推進到 0）
        if (timeMs_ >= 0.0) {
            started_ = true;
            startedThisTick_ = true;
            timeMs_ = 0.0;
        }
        return;
    }

    timeMs_ += frameDeltaMs;  // 平滑前進
    if (audioPosMs >= 0.0) {
        const double err = audioPosMs - timeMs_;
        if (std::abs(err) > 80.0) {
            timeMs_ = audioPosMs;  // 大幅偏離（開頭/卡頓）直接對齊
        } else {
            timeMs_ += err * 0.05;  // 平滑收斂回音訊
        }
    }
}

PlaySession::PlaySession(std::vector<ManiaNote> notes)
    : notes_(std::move(notes)), state_(notes_.size(), NoteState::Idle) {
    holding_.fill(-1);
    for (const ManiaNote& n : notes_) {
        const int last = (n.endTime > 0) ? n.endTime : n.startTime;
        songEndMs_ = std::max(songEndMs_, static_cast<double>(last));
        totalUnits_ += (n.endTime > 0) ? 2 : 1;  // 長押算頭、尾兩個判定單位
    }
    songEndMs_ += kEndPadMs;
}

void PlaySession::press(int lane, double songTimeMs) {
    // 找該軌道中最接近現在、落在判定視窗內、尚未判定的音符頭
    int best = -1;
    double bestAbs = kHitWindowMs + 1.0;
    for (std::size_t i = 0; i < notes_.size(); ++i) {
        if (state_[i] != NoteState::Idle || notes_[i].column != lane) continue;
        const double d = std::abs(notes_[i].startTime - songTimeMs);
        if (d <= kHitWindowMs && d < bestAbs) {
            bestAbs = d;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return;

    recordError(songTimeMs - notes_[best].startTime);  // >0 = 偏晚按

    const Judgment headJ = judgeByError(bestAbs);
    addJudgment(headJ);
    if (headJ != Judgment::Miss) events_.push_back({lane, headJ});

    if (notes_[best].endTime > 0) {
        state_[best] = NoteState::Holding;  // 長押：開始按住，尾部待判
        holding_[lane] = best;
    } else {
        state_[best] = NoteState::Done;
    }
}

void PlaySession::release(int lane, double songTimeMs) {
    const int idx = holding_[lane];
    if (idx < 0) return;  // 該軌道沒有正在按住的長押

    const ManiaNote& n = notes_[idx];
    state_[idx] = NoteState::Done;
    holding_[lane] = -1;

    if (songTimeMs < n.endTime - kHitWindowMs) {
        addJudgment(Judgment::Miss);  // 提早放開
    } else {
        const double err = songTimeMs - n.endTime;
        recordError(err);
        const Judgment tailJ = judgeByError(std::abs(err));
        addJudgment(tailJ);
        if (tailJ != Judgment::Miss) events_.push_back({lane, tailJ});
    }
}

void PlaySession::advance(double songTimeMs) {
    for (std::size_t i = 0; i < notes_.size(); ++i) {
        const ManiaNote& n = notes_[i];
        switch (state_[i]) {
            case NoteState::Idle:
                // 頭部通過最大視窗仍未按 → Miss（長押則頭尾都算漏）
                if (songTimeMs - n.startTime > kHitWindowMs) {
                    state_[i] = NoteState::Done;
                    addJudgment(Judgment::Miss);
                    if (n.endTime > 0) addJudgment(Judgment::Miss);
                }
                break;
            case NoteState::Holding:
                // 一路按到尾端 → 尾部 Perfect（提早放開在 release 處理）
                if (songTimeMs >= n.endTime) {
                    state_[i] = NoteState::Done;
                    holding_[n.column] = -1;
                    addJudgment(Judgment::Perfect);
                    events_.push_back({n.column, Judgment::Perfect});
                }
                break;
            case NoteState::Done:
                break;
        }
    }
}

std::vector<PlaySession::HitEvent> PlaySession::drainEvents() {
    return std::exchange(events_, {});
}

Judgment PlaySession::judgeByError(double absErrMs) const {
    for (int i = 0; i < 4; ++i) {
        if (absErrMs <= kWindowMs[i]) return kTiers[i];
    }
    return Judgment::Miss;
}

void PlaySession::recordError(double signedErrMs) {
    signedErrSum_ += signedErrMs;
    ++errSamples_;
    const double half = kHistBins / 2.0;
    int bin = static_cast<int>(std::lround(signedErrMs / kHitWindowMs * half) + half);
    bin = std::clamp(bin, 0, kHistBins - 1);
    ++hist_[bin];
}

void PlaySession::addJudgment(Judgment j) {
    if (j == Judgment::None) return;
    lastJudge_ = j;
    ++counts_[static_cast<int>(j)];
    const int pts = kPoints[static_cast<int>(j)];
    score_ += pts;
    pointsAccum_ += pts;
    if (j == Judgment::Miss) {
        combo_ = 0;
    } else {
        ++combo_;
        maxCombo_ = std::max(maxCombo_, combo_);
    }
}

double PlaySession::accuracy() const {
    if (totalUnits_ == 0) return 0.0;
    return static_cast<double>(pointsAccum_) / (totalUnits_ * 300) * 100.0;
}

const char* PlaySession::grade() const {
    const auto c = [&](Judgment j) { return counts_[static_cast<int>(j)]; };
    if (c(Judgment::Miss) == 0 && c(Judgment::Bad) == 0 && c(Judgment::Good) == 0 &&
        c(Judgment::Great) == 0 && c(Judgment::Perfect) > 0) {
        return "SS";
    }
    const double acc = accuracy();
    if (acc >= 95.0) return "S";
    if (acc >= 90.0) return "A";
    if (acc >= 80.0) return "B";
    if (acc >= 70.0) return "C";
    return "D";
}
