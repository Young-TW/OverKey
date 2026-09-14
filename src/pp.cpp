// osu!mania pp / Quaver rating 估算的數學核心。
// Quaver 難度部分為 Quaver.API QSS DifficultyProcessorKeys v0.0.5 的 C++ 移植
// （https://github.com/Quaver/Quaver.API），含上游已知行為：
//   1. 奇數鍵數（7K）會把整個解算流程跑兩次，且第二次疊加在累積資料上；
//   2. jack manipulation 寫入的是 RollManipulationStrainMultiplier 欄位；
//   3. chord 合併時不移除重複 finger、也不更新 EndTime。
// 這些看似 bug 的行為即線上 rating 的實際行為，故原樣保留。

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "pp.h"
#include "play.h"

namespace {

// ---- QSS v0.0.5 常數（StrainConstantsKeys 預設值）----
constexpr float kLnEndThresholdMs = 42.0f;
constexpr float kChordClumpToleranceMs = 8.0f;

constexpr float kSJackLower = 40, kSJackUpper = 320, kSJackMax = 68, kSJackExp = 1.17f;
constexpr float kTJackLower = 40, kTJackUpper = 330, kTJackMax = 70, kTJackExp = 1.14f;
constexpr float kRollLower = 30, kRollUpper = 230, kRollMax = 55, kRollExp = 1.13f;
constexpr float kBracketLower = 30, kBracketUpper = 230, kBracketMax = 56, kBracketExp = 1.13f;

constexpr float kLnBaseMult = 0.6f, kLnLayerTolerance = 60.0f, kLnLayerThreshold = 93.7f;
constexpr float kLnReleaseAfter = 1.0f, kLnReleaseBefore = 1.3f, kLnTap = 1.05f;

constexpr float kVibroDuration = 88.2f, kVibroTolerance = 88.2f;
constexpr float kVibroMult = 0.75f, kVibroLengthMult = 0.3f, kVibroMaxLength = 6.0f;

constexpr float kRollRatioTolerance = 2.0f, kRollRatioMult = 0.25f;
constexpr float kRollLengthMult = 0.6f, kRollMaxLength = 14.0f;

enum class QHand { Left, Right };

// 手指位元（Quaver FingerState：Index=1 .. Thumb=16）
constexpr int kIndex = 1, kMiddle = 2, kRing = 4, kPinkie = 8, kThumb = 16;

enum class QAction { None, SimpleJack, TechnicalJack, Roll, Bracket };

struct QHit {
    float start = 0, end = 0;  // /rate 後的時間；tap 的 end = 0（同 Quaver HitObjectInfo）
    int lane = 1;              // 1-based
    int finger = 0;
    float lnMult = 1.0f;
    float strain = 0.0f;
};

struct QData {
    std::vector<QHit> hits;
    int next = -1;  // list 中同手下一個 data point 的 index；-1 = 無
    float startTime = 0, endTime = 0;
    float coeff = 1.0f;
    float rollManipMult = 1.0f;  // 上游 jack manipulation 也寫這裡（pattern/jackMult 恆為 1）
    float totalStrain = 0.0f;    // 重要：多次 CalculateStrainValue 會累加（上游行為）
    QHand hand = QHand::Right;
    QAction action = QAction::None;
    float actionDurMs = 0.0f;
    int fingerState = 0;
    bool handChord() const { return hits.size() > 1; }
};

// lane 為 1-based（Quaver 慣例）
int laneToFinger(int lane, int keyCount) {
    const int half = keyCount / 2;
    if (keyCount <= 9) {
        if (keyCount % 2 == 0) {
            if (lane <= half) return 1 << (half - lane);
            return 1 << (lane - (half + 1));
        }
        if (lane <= half) return 1 << (half - lane);
        if (lane == half + 1) return kThumb;
        return 1 << (lane - (half + 2));
    }
    return kThumb;
}

int laneToHand(int lane, int keyCount) {  // 0=Left 1=Right 2=Ambiguous
    const int half = keyCount / 2;
    if (keyCount % 2 == 0) return (lane <= half) ? 0 : 1;
    if (lane <= half) return 0;
    if (lane == half + 1) return 2;
    return 1;
}

class Qss {
public:
    Qss(const std::vector<ManiaNote>& notes, int keyCount) : notes_(notes), keyCount_(keyCount) {
        mapLength_ = 0;
        for (const ManiaNote& n : notes_) {
            mapLength_ =
                std::max(mapLength_, std::max(n.startTime, n.endTime > 0 ? n.endTime : 0));
        }
    }

    float compute(float rate, QHand assumeHand) {
        computeNoteDensity(rate);
        // If this is the first pass, build the base data structures.
        if (data_.empty()) {
            computeBaseStrainStates(rate, assumeHand);
        } else {
            // Subsequent passes: reassign ambiguous hand values and reset per‑pass fields.
            for (auto &d : data_) {
                // Re‑assign hand for ambiguous lanes (previously marked as Ambiguous).
                if (d.hand == QHand::Left || d.hand == QHand::Right) {
                    // keep existing assignment
                } else {
                    // ambiguous – set according to current assumeHand
                    d.hand = assumeHand;
                }
                // Reset transient calculation fields.
                d.next = -1;
                d.actionDurMs = 0.0f;
                d.coeff = 1.0f;
                d.action = QAction::None;
                d.rollManipMult = 1.0f;
                // d.totalStrain is retained to accumulate across passes as intended by spec
                // fingerState will be recomputed in computeForChords.
            }
        }
        computeForChords();
        computeForFingerActions();
        computeForRollManipulation();
        computeForJackManipulation();
        computeForLnMultiplier();
        return calculateOverallDifficulty();
    }

private:
    void computeNoteDensity(float rate) {
        // AverageNoteDensity = 1000 * contributing / (length * (-0.5*rate + 1.5))
        if (mapLength_ <= 0) {
            avgDensity_ = 0;
            return;
        }
        avgDensity_ = 1000.0f * static_cast<float>(notes_.size()) /
                      (static_cast<float>(mapLength_) * (-0.5f * rate + 1.5f));
    }

    void computeBaseStrainStates(float rate, QHand assumeHand) {
        for (const ManiaNote& n : notes_) {
            QHit hit;
            hit.start = static_cast<float>(n.startTime) / rate;
            hit.end = static_cast<float>(n.endTime > 0 ? n.endTime : 0) / rate;
            hit.lane = n.column + 1;
            hit.finger = laneToFinger(hit.lane, keyCount_);

            QData d;
            d.hits.push_back(hit);
            d.startTime = hit.start;
            d.endTime = hit.end;
            const int h = laneToHand(hit.lane, keyCount_);
            d.hand = (h == 2) ? assumeHand : static_cast<QHand>(h);
            data_.push_back(std::move(d));
        }
    }

    void computeForChords() {
        // 上游為索引 for-loop，RemoveAt(j) 後 j++ —— 會跳過補位元素；原樣保留
        for (std::size_t i = 0; i + 1 < data_.size(); ++i) {
            for (std::size_t j = i + 1; j < data_.size(); ++j) {
                const float msDiff = data_[j].startTime - data_[i].startTime;
                if (msDiff > kChordClumpToleranceMs) break;
                if (std::fabs(msDiff) <= kChordClumpToleranceMs &&
                    data_[i].hand == data_[j].hand) {
                    for (const QHit& k : data_[j].hits) {
                        bool sameStateFound = false;
                        for (const QHit& l : data_[i].hits) {
                            if (l.finger == k.finger) sameStateFound = true;
                        }
                        if (!sameStateFound) data_[i].hits.push_back(k);
                    }
                    data_.erase(data_.begin() + static_cast<std::ptrdiff_t>(j));
                }
            }
        }
        for (QData& d : data_) {
            d.fingerState = 0;  // 上游為 |=（未重置）；此處每位元冪等，結果相同
            for (const QHit& h : d.hits) d.fingerState |= h.finger;
        }
    }

    float getCoefficientValue(float duration, float xMin, float xMax, float strainMax,
                              float exp) const {
        constexpr float lowestDifficulty = 1.0f;
        constexpr float densityMultiplier = 0.266f;
        constexpr float densityDifficultyMin = 0.4f;
        const float ratio = std::max(0.0f, 1.0f - (duration - xMin) / (xMax - xMin));
        if (ratio == 0.0f && avgDensity_ < 4.0f) {
            if (avgDensity_ < 1.0f) return densityDifficultyMin;
            return avgDensity_ * densityMultiplier + 0.134f;
        }
        return lowestDifficulty + (strainMax - lowestDifficulty) * std::pow(ratio, exp);
    }

    void computeForFingerActions() {
        for (std::size_t i = 0; i + 1 < data_.size(); ++i) {
            QData& cur = data_[i];
            for (std::size_t j = i + 1; j < data_.size(); ++j) {
                QData& nxt = data_[j];
                if (cur.hand == nxt.hand && nxt.startTime > cur.startTime) {
                    const bool actionJackFound =
                        (cur.fingerState & nxt.fingerState) != 0;
                    const bool actionChordFound = cur.handChord() || nxt.handChord();
                    const bool actionSameState = cur.fingerState == nxt.fingerState;
                    const float actionDuration = nxt.startTime - cur.startTime;

                    cur.next = static_cast<int>(j);
                    cur.actionDurMs = actionDuration;

                    if (!actionChordFound && !actionSameState) {
                        cur.action = QAction::Roll;
                        cur.coeff = getCoefficientValue(actionDuration, kRollLower, kRollUpper,
                                                        kRollMax, kRollExp);
                    } else if (actionSameState) {
                        cur.action = QAction::SimpleJack;
                        cur.coeff = getCoefficientValue(actionDuration, kSJackLower, kSJackUpper,
                                                        kSJackMax, kSJackExp);
                    } else if (actionJackFound) {
                        cur.action = QAction::TechnicalJack;
                        cur.coeff = getCoefficientValue(actionDuration, kTJackLower, kTJackUpper,
                                                        kTJackMax, kTJackExp);
                    } else {
                        cur.action = QAction::Bracket;
                        cur.coeff = getCoefficientValue(actionDuration, kBracketLower,
                                                        kBracketUpper, kBracketMax, kBracketExp);
                    }
                    break;
                }
            }
        }
    }

    void computeForRollManipulation() {
        float manipulationIndex = 0.0f;
        for (QData& d : data_) {
            bool manipulationFound = false;
            if (d.next >= 0 && data_[d.next].next >= 0) {
                const QData& middle = data_[d.next];
                const QData& last = data_[middle.next];
                if (d.action == QAction::Roll && middle.action == QAction::Roll &&
                    d.fingerState == last.fingerState) {
                    const float durationRatio =
                        std::max(d.actionDurMs / middle.actionDurMs,
                                 middle.actionDurMs / d.actionDurMs);
                    if (durationRatio >= kRollRatioTolerance) {
                        const float durationMultiplier =
                            1.0f / (1.0f + (durationRatio - 1.0f) * kRollRatioMult);
                        const float manipulationFoundRatio =
                            1.0f - manipulationIndex / kRollMaxLength * (1.0f - kRollLengthMult);
                        d.rollManipMult = durationMultiplier * manipulationFoundRatio;
                        manipulationFound = true;
                        if (manipulationIndex < kRollMaxLength) manipulationIndex += 1.0f;
                    }
                }
            }
            if (!manipulationFound && manipulationIndex > 0.0f) manipulationIndex -= 1.0f;
        }
    }

    void computeForJackManipulation() {
        float longJackSize = 0.0f;
        for (QData& d : data_) {
            bool manipulationFound = false;
            if (d.next >= 0) {
                const QData& nxt = data_[d.next];
                if (d.action == QAction::SimpleJack && nxt.action == QAction::SimpleJack) {
                    const float durationValue =
                        std::min(1.0f, std::max(0.0f, (kVibroDuration + kVibroTolerance -
                                                       d.actionDurMs) /
                                                          kVibroTolerance));
                    const float durationMultiplier =
                        1.0f - durationValue * (1.0f - kVibroMult);
                    const float manipulationFoundRatio =
                        1.0f - longJackSize / kVibroMaxLength * (1.0f - kVibroLengthMult);
                    // 上游將 jack manipulation 寫入 roll 欄位（bug-for-bug）
                    d.rollManipMult = durationMultiplier * manipulationFoundRatio;
                    manipulationFound = true;
                    if (longJackSize < kVibroMaxLength) longJackSize += 1.0f;
                }
            }
            if (!manipulationFound) longJackSize = 0.0f;
        }
    }

    void computeForLnMultiplier() {
        for (QData& d : data_) {
            if (d.endTime > d.startTime) {
                const float durationValue =
                    1.0f - std::min(1.0f, std::max(0.0f, (kLnLayerThreshold + kLnLayerTolerance -
                                                          (d.endTime - d.startTime)) /
                                                             kLnLayerTolerance));
                const float baseMultiplier = 1.0f + durationValue * kLnBaseMult;
                for (QHit& k : d.hits) k.lnMult = baseMultiplier;

                if (d.next >= 0) {
                    const QData& nxt = data_[d.next];
                    if (nxt.startTime < d.endTime - kLnEndThresholdMs) {
                        if (nxt.startTime >= d.startTime + kLnEndThresholdMs) {
                            if (nxt.endTime > d.endTime + kLnEndThresholdMs) {
                                for (QHit& k : d.hits) k.lnMult *= kLnReleaseAfter;
                            } else if (nxt.endTime > 0.0f) {
                                for (QHit& k : d.hits) k.lnMult *= kLnReleaseBefore;
                            } else {
                                for (QHit& k : d.hits) k.lnMult *= kLnTap;
                            }
                        }
                    }
                }
            }
        }
    }

    float calculateOverallDifficulty() {
        if (data_.empty()) return 0.0f;

        double sum = 0.0;
        for (QData& d : data_) {
            // 上游：CalculateStrainValue 對 totalStrain 累加後再除以 hits 數；
            // 多次呼叫會在上次結果上累加（7K 第二次運算的上游行為）
            float add = 0.0f;
            for (const QHit& h : d.hits) add += d.coeff * d.rollManipMult * h.lnMult;
            d.totalStrain += add / static_cast<float>(d.hits.size());
            sum += d.totalStrain;
        }
        float calculatedDiff = static_cast<float>(sum / data_.size());

        const int binSize = 1000;
        std::vector<float> bins;
        float mapStart = data_.front().startTime, mapEnd = 0.0f;
        for (const QData& d : data_) {
            mapStart = std::min(mapStart, d.startTime);
            mapEnd = std::max(mapEnd, std::max(d.startTime, d.endTime));
        }

        std::size_t leftIndex = 0, rightIndex = 0;
        const bool useFallback = (keyCount_ % 2 == 1);
        while (leftIndex < data_.size() && data_[leftIndex].startTime < mapStart) ++leftIndex;
        for (float i = mapStart; i < mapEnd; i += binSize) {
            float binSum = 0.0f;
            int binCount = 0;
            if (useFallback) {
                for (const QData& d : data_) {
                    if (d.startTime >= i && d.startTime < i + binSize) {
                        binSum += d.totalStrain;
                        ++binCount;
                    }
                }
            } else {
                while (rightIndex + 1 < data_.size() &&
                       data_[rightIndex + 1].startTime < i + binSize)
                    ++rightIndex;
                if (leftIndex >= data_.size()) {
                    bins.push_back(0.0f);
                    continue;
                }
                for (std::size_t k = leftIndex; k <= rightIndex; ++k) {
                    binSum += data_[k].totalStrain;
                    ++binCount;
                }
                leftIndex = rightIndex + 1;
            }
            bins.push_back(binCount > 0 ? binSum / binCount : 0.0f);
        }

        const bool any = std::any_of(bins.begin(), bins.end(),
                                     [](float s) { return s > 0.0f; });
        if (!any) return 0.0f;

        // continuity：以最難的 40% 區段為基準
        std::vector<float> sorted = bins;
        std::sort(sorted.begin(), sorted.end(), std::greater<float>());
        const std::size_t cutoffPos = sorted.size() * 2 / 5;  // floor(count*0.4)
        double cutoffSum = 0.0;
        for (std::size_t i = 0; i < cutoffPos; ++i) cutoffSum += sorted[i];
        const double easyRatingCutoff = cutoffPos > 0 ? cutoffSum / cutoffPos : 0.0;

        double contSum = 0.0;
        int contCount = 0;
        for (float s : bins) {
            if (s > 0.0f) {
                contSum += std::sqrt(static_cast<double>(s) / easyRatingCutoff);
                ++contCount;
            }
        }
        const float continuity = static_cast<float>(contSum / contCount);

        constexpr float maxContinuity = 1.00f, avgContinuity = 0.85f, minContinuity = 0.60f;
        constexpr float maxAdjustment = 1.05f, avgAdjustment = 1.00f, minAdjustment = 0.90f;
        float continuityAdjustment;
        if (continuity > avgContinuity) {
            const float continuityFactor =
                1.0f - (continuity - avgContinuity) / (maxContinuity - avgContinuity);
            continuityAdjustment =
                std::min(avgAdjustment,
                         std::max(minAdjustment, continuityFactor * (avgAdjustment -
                                                                     minAdjustment) +
                                                     minAdjustment));
        } else {
            const float continuityFactor =
                1.0f - (continuity - minContinuity) / (avgContinuity - minContinuity);
            continuityAdjustment =
                std::min(maxAdjustment,
                         std::max(avgAdjustment, continuityFactor * (maxAdjustment -
                                                                     avgAdjustment) +
                                                     avgAdjustment));
        }
        calculatedDiff *= continuityAdjustment;

        // 短圖 nerf（以 continuity 推估真實 drain time）
        constexpr float maxShortMapAdjustment = 0.75f;
        constexpr float shortMapThreshold = 60000.0f;
        const float trueDrainTime = bins.size() * continuity * binSize;
        const float shortMapAdjustment = std::min(
            1.0f, std::max(maxShortMapAdjustment,
                           0.25f * std::sqrt(trueDrainTime / shortMapThreshold) + 0.75f));
        calculatedDiff *= shortMapAdjustment;

        return calculatedDiff;
    }

    const std::vector<ManiaNote>& notes_;
    int keyCount_;
    int mapLength_ = 0;
    float avgDensity_ = 0.0f;
    std::vector<QData> data_;  // 上游為成員且跨次運算累積（7K 第二次運算疊加於此）
};

}  // namespace

double quaverDifficulty(const std::vector<ManiaNote>& notes, int keyCount, float rate) {
    if (notes.size() < 2) return 0.0;
    if (rate <= 0.0f) rate = 1.0f;

    // 上游依賴已排序的 HitObjects（Qua 載入時會排序）；這裡對穩定副本排序
    std::vector<ManiaNote> sorted = notes;
    std::stable_sort(sorted.begin(), sorted.end(), [](const ManiaNote& a, const ManiaNote& b) {
        return a.startTime < b.startTime;
    });

    Qss solver(sorted, keyCount);
    if (keyCount % 2 == 0) return solver.compute(rate, QHand::Right);
    // 奇數鍵數：左右手各跑一次並平均；第二次運算疊加在累積資料上（上游行為）
    const float a = solver.compute(rate, QHand::Left);
    const float b = solver.compute(rate, QHand::Right);
    return (a + b) / 2.0;
}

double quaverAccuracy(int nMarv, int nPerf, int nGreat, int nGood, int nOkay, int nMiss) {
    const double total = nMarv + nPerf + nGreat + nGood + nOkay + nMiss;
    if (total <= 0) return 0.0;
    const double sum = 100.0 * nMarv + 98.25 * nPerf + 65.0 * nGreat + 25.0 * nGood -
                       100.0 * nOkay - 50.0 * nMiss;
    return std::max(sum / (total * 100.0), 0.0) * 100.0;
}

double quaverRating(double difficulty, double accuracy) {
    return difficulty * std::pow(accuracy / 98.0, 6.0);
}

double osuManiaCustomAccuracy(int n320, int n300, int n200, int n100, int n50, int nMiss) {
    const double total = n320 + n300 + n200 + n100 + n50 + nMiss;
    if (total <= 0) return 0.0;
    return (320.0 * n320 + 300.0 * n300 + 200.0 * n200 + 100.0 * n100 + 50.0 * n50) /
           (total * 320.0);
}

double osuManiaPP(double starRating, int n320, int n300, int n200, int n100, int n50,
                  int nMiss) {
    const double totalHits = n320 + n300 + n200 + n100 + n50 + nMiss;
    if (totalHits <= 0) return 0.0;
    const double acc = std::clamp(
        osuManiaCustomAccuracy(n320, n300, n200, n100, n50, nMiss), 0.0, 1.0);
    const double difficultyValue =
        8.0 * std::pow(std::max(starRating - 0.15, 0.05), 2.2) *
        std::max(0.0, 5.0 * acc - 4.0) * (1.0 + 0.1 * std::min(1.0, totalHits / 1500.0));
    return difficultyValue;  // 無 NoFail/Easy 時 multiplier = 1.0
}

double osuEstimateStarRating(double quaverDifficulty) { return quaverDifficulty / 4.4; }

RatingEstimate estimateRatings(double quaverDiff, const PlaySession& session) {
    RatingEstimate r;
    r.quaverDiff = quaverDiff;

    // 判定對應（見 pp.h）：OverKey 各級對到兩遊戲「次高」以下相同窗寬的等級
    const int nPerfect = session.count(Judgment::Perfect);
    const int nGreat = session.count(Judgment::Great);
    const int nGood = session.count(Judgment::Good);
    const int nBad = session.count(Judgment::Bad);
    const int nMiss = session.count(Judgment::Miss);

    // Quaver：Perfect→Perf / Great→Great / Good→Good / Bad→Okay（Marv 視為 0）
    r.quaverAcc = quaverAccuracy(0, nPerfect, nGreat, nGood, nBad, nMiss);
    r.quaverRating = quaverRating(quaverDiff, r.quaverAcc);

    // osu!mania：Perfect→300 / Great→200 / Good→100 / Bad→50（320g 視為 0）
    r.osuStar = osuEstimateStarRating(quaverDiff);
    r.osuPP = osuManiaPP(r.osuStar, 0, nPerfect, nGreat, nGood, nBad, nMiss);
    return r;
}
