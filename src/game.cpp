#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include <raylib.h>

#include "game.h"
#include "raii.h"

namespace {

// ---- 版面 / 玩法常數 ----
constexpr int kCols = 7;
constexpr int kLaneW = 80;
constexpr int kPlayfieldW = kCols * kLaneW;            // 560
constexpr int kScreenW = kPlayfieldW + 360;           // 兩側留 HUD 空間
constexpr int kScreenH = 920;
constexpr int kOriginX = (kScreenW - kPlayfieldW) / 2;
constexpr float kJudgeY = kScreenH - 140.0f;          // 判定線 Y
constexpr float kNoteH = 22.0f;

constexpr double kBaseApproachMs = 550.0;              // scrollSpeed=1 時的下落時間
constexpr double kLeadInMs = 2000.0;                   // 開場倒數，避免首批音符瞬間出現
constexpr double kEndPadMs = 1500.0;                   // 最後音符後到結算的餘量

// 五級判定：依 |誤差| 由嚴到鬆，超過最後一級即 Miss
using J = Game::Judgment;
constexpr double kWindowMs[] = {35.0, 65.0, 95.0, 125.0};       // Perfect/Great/Good/Bad
constexpr J kTiers[] = {J::Perfect, J::Great, J::Good, J::Bad};
constexpr double kHitWindowMs = 125.0;                          // 最大判定視窗
constexpr int kPoints[] = {0, 300, 200, 100, 50, 0};           // 對應 Judgment 列舉

const char* judgeName(J j) {
    switch (j) {
        case J::Perfect: return "PERFECT";
        case J::Great:   return "GREAT";
        case J::Good:    return "GOOD";
        case J::Bad:     return "BAD";
        case J::Miss:    return "MISS";
        default:         return "";
    }
}

Color judgeColor(J j) {
    switch (j) {
        case J::Perfect: return SKYBLUE;
        case J::Great:   return GREEN;
        case J::Good:    return GOLD;
        case J::Bad:     return ORANGE;
        case J::Miss:    return RED;
        default:         return GRAY;
    }
}

constexpr Color kLaneColors[kCols] = {
    {60, 60, 70, 255},  {45, 45, 55, 255},  {60, 60, 70, 255},
    {70, 55, 80, 255},
    {60, 60, 70, 255},  {45, 45, 55, 255},  {60, 60, 70, 255},
};

constexpr Color kNoteColors[kCols] = {
    SKYBLUE, WHITE, SKYBLUE, GOLD, SKYBLUE, WHITE, SKYBLUE,
};

constexpr double kFlashDur = 0.18;  // 命中閃光持續秒數

// 程式合成一個短促的打擊聲（正弦 blip + 起音雜訊，快速衰減），免外部素材
Sound makeHitSound() {
    constexpr int sr = 44100;
    constexpr float dur = 0.05f;
    constexpr int n = static_cast<int>(sr * dur);
    auto* data = static_cast<short*>(std::malloc(n * sizeof(short)));
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / sr;
        const float env = std::exp(-t * 55.0f);
        const float tone = std::sin(2.0f * PI * 880.0f * t);
        const float noise = (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
        const float s = env * (0.6f * tone + 0.4f * noise * std::exp(-t * 180.0f));
        data[i] = static_cast<short>(std::clamp(s, -1.0f, 1.0f) * 28000.0f);
    }
    const Wave wave{static_cast<unsigned>(n), static_cast<unsigned>(sr), 16, 1, data};
    const Sound snd = LoadSoundFromWave(wave);
    UnloadWave(wave);  // 釋放 data
    return snd;
}

float laneX(int col) { return kOriginX + col * kLaneW; }

// 給定音符時間，回傳此刻其頭部中心應在的 Y
float noteY(int noteTimeMs, double songTimeMs, double pxPerMs) {
    return kJudgeY - static_cast<float>((noteTimeMs - songTimeMs) * pxPerMs);
}

}  // namespace

Game::Game(Beatmap map, std::filesystem::path audioPath, Settings settings)
    : map_(std::move(map)),
      audioPath_(std::move(audioPath)),
      settings_(settings),
      offsetMs_(settings.audioOffsetMs),
      approachMs_(kBaseApproachMs / settings.scrollSpeed),
      pxPerMs_(kJudgeY / approachMs_),
      state_(map_.notes.size(), NoteState::Idle) {
    holding_.fill(-1);
    laneFlash_.fill(-10.0);
    for (const ManiaNote& n : map_.notes) {
        const int last = (n.endTime > 0) ? n.endTime : n.startTime;
        songEndMs_ = std::max(songEndMs_, static_cast<double>(last));
        totalUnits_ += (n.endTime > 0) ? 2 : 1;  // 長押算頭、尾兩個判定單位
    }
    songEndMs_ += kEndPadMs;
}

void Game::run() {
    // 視窗與音訊裝置由外層（main）建立並持有；這裡只負責一局的邏輯。
    if (!map_.title.empty()) SetWindowTitle(("OverKey - " + map_.title).c_str());

    MusicRes music{audioPath_.string().c_str()};
    const bool haveMusic = music.valid();

    SoundRes hitSound{makeHitSound()};

    const double startWall = GetTime() + kLeadInMs / 1000.0;  // 真正 t=0 的牆鐘時間
    bool musicStarted = false;
    double songTimeMs = -kLeadInMs;

    while (!WindowShouldClose()) {
        if (phase_ == Phase::Playing) {
            if (IsKeyPressed(KEY_ESCAPE)) {  // 中途放棄，回選單
                if (musicStarted) StopMusicStream(music.get());
                return;
            }
            // 時鐘：開場用牆鐘倒數到 0；音樂開始後改以音訊播放位置為準，避免飄移
            songTimeMs = (GetTime() - startWall) * 1000.0;
            if (haveMusic) {
                if (!musicStarted && songTimeMs >= 0.0) {
                    PlayMusicStream(music.get());
                    musicStarted = true;
                }
                if (musicStarted) {
                    UpdateMusicStream(music.get());
                    songTimeMs = GetMusicTimePlayed(music.get()) * 1000.0;
                }
            }
            songTimeMs += offsetMs_;  // 套用音訊偏移，渲染與判定共用同一時鐘

            update(songTimeMs);

            if (hitSoundQueued_) {
                PlaySound(hitSound.get());
                hitSoundQueued_ = false;
            }

            if (songTimeMs >= songEndMs_) {
                phase_ = Phase::Result;
                if (musicStarted) StopMusicStream(music.get());
            }
        } else {  // Result：Enter/Esc 回選單
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                IsKeyPressed(KEY_ESCAPE)) {
                return;
            }
        }

        BeginDrawing();
        ClearBackground(Color{18, 18, 24, 255});
        if (phase_ == Phase::Playing) {
            drawPlayfield(songTimeMs);
        } else {
            drawResult();
        }
        EndDrawing();
    }
}

void Game::update(double songTimeMs) {
    // 輸入：按下判頭，放開判長押尾
    for (int c = 0; c < kCols; ++c) {
        if (IsKeyPressed(settings_.keys[c])) judgePress(c, songTimeMs);
        if (IsKeyReleased(settings_.keys[c])) judgeRelease(c, songTimeMs);
    }

    // 逐音符推進狀態
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        const ManiaNote& n = map_.notes[i];
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
                // 一路按到尾端 → 尾部 Perfect（提早放開會在 judgeRelease 處理）
                if (songTimeMs >= n.endTime) {
                    state_[i] = NoteState::Done;
                    holding_[n.column] = -1;
                    addJudgment(Judgment::Perfect);
                    triggerFlash(n.column, Judgment::Perfect);
                }
                break;
            case NoteState::Done:
                break;
        }
    }
}

void Game::judgePress(int column, double songTimeMs) {
    // 找該軌道中最接近現在、落在判定視窗內、尚未判定的音符頭
    int best = -1;
    double bestAbs = kHitWindowMs + 1.0;
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        if (state_[i] != NoteState::Idle || map_.notes[i].column != column) continue;
        const double d = std::abs(map_.notes[i].startTime - songTimeMs);
        if (d <= kHitWindowMs && d < bestAbs) {
            bestAbs = d;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return;

    recordError(songTimeMs - map_.notes[best].startTime);  // >0 = 偏晚按

    const Judgment headJ = judgeByError(bestAbs);
    addJudgment(headJ);  // 頭部判定
    if (headJ != Judgment::Miss) triggerFlash(column, headJ);
    if (map_.notes[best].endTime > 0) {
        state_[best] = NoteState::Holding;  // 長押：開始按住，尾部待判
        holding_[column] = best;
    } else {
        state_[best] = NoteState::Done;
    }
}

void Game::judgeRelease(int column, double songTimeMs) {
    const int idx = holding_[column];
    if (idx < 0) return;  // 該軌道沒有正在按住的長押

    const ManiaNote& n = map_.notes[idx];
    state_[idx] = NoteState::Done;
    holding_[column] = -1;

    if (songTimeMs < n.endTime - kHitWindowMs) {
        addJudgment(Judgment::Miss);  // 提早放開
    } else {
        const double err = songTimeMs - n.endTime;
        recordError(err);
        const Judgment tailJ = judgeByError(std::abs(err));  // 在尾端視窗內放開
        addJudgment(tailJ);
        if (tailJ != Judgment::Miss) triggerFlash(column, tailJ);
    }
}

Game::Judgment Game::judgeByError(double absErrMs) const {
    for (int i = 0; i < 4; ++i) {
        if (absErrMs <= kWindowMs[i]) return kTiers[i];
    }
    return Judgment::Miss;
}

void Game::recordError(double signedErrMs) {
    signedErrSum_ += signedErrMs;
    ++errSamples_;
    const double half = kHistBins / 2.0;
    int bin = static_cast<int>(std::lround(signedErrMs / kHitWindowMs * half) + half);
    bin = std::clamp(bin, 0, kHistBins - 1);
    ++hist_[bin];
}

void Game::addJudgment(Judgment j) {
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
        hitSoundQueued_ = true;
        maxCombo_ = std::max(maxCombo_, combo_);
    }
}

void Game::triggerFlash(int column, Judgment j) {
    laneFlash_[column] = GetTime();
    laneFlashJudge_[column] = j;
}

double Game::accuracy() const {
    if (totalUnits_ == 0) return 0.0;
    return static_cast<double>(pointsAccum_) / (totalUnits_ * 300) * 100.0;
}

const char* Game::grade() const {
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

void Game::drawPlayfield(double songTimeMs) const {
    // 軌道底色（按下時提亮）
    for (int c = 0; c < kCols; ++c) {
        Color col = kLaneColors[c];
        if (IsKeyDown(settings_.keys[c])) {
            col = Color{static_cast<unsigned char>(std::min(255, col.r + 40)),
                        static_cast<unsigned char>(std::min(255, col.g + 40)),
                        static_cast<unsigned char>(std::min(255, col.b + 40)), 255};
        }
        DrawRectangle(static_cast<int>(laneX(c)), 0, kLaneW, kScreenH, col);
    }
    for (int c = 0; c <= kCols; ++c) {
        DrawLine(static_cast<int>(laneX(c)), 0, static_cast<int>(laneX(c)), kScreenH,
                 Color{30, 30, 38, 255});
    }

    // 命中閃光（判定線上的淡出光暈，依判定著色）
    const double now = GetTime();
    for (int c = 0; c < kCols; ++c) {
        const double dt = now - laneFlash_[c];
        if (dt < 0.0 || dt >= kFlashDur) continue;
        const float a = static_cast<float>(1.0 - dt / kFlashDur);
        const Color base = judgeColor(laneFlashJudge_[c]);
        const int x = static_cast<int>(laneX(c));
        // 向上漸層的光柱 + 判定線上的亮條
        DrawRectangleGradientV(x, static_cast<int>(kJudgeY) - 160, kLaneW, 160,
                               Fade(base, 0.0f), Fade(base, 0.35f * a));
        DrawRectangle(x, static_cast<int>(kJudgeY) - 6, kLaneW, 12, Fade(base, 0.8f * a));
    }

    // 判定線
    DrawRectangle(kOriginX, static_cast<int>(kJudgeY) - 3, kPlayfieldW, 6, RAYWHITE);

    // 音符（含長押尾巴），只畫尚未完成、且在畫面範圍內的
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        if (state_[i] == NoteState::Done) continue;
        const ManiaNote& n = map_.notes[i];
        // 按住中的長押：頭部固定在判定線，身體隨尾端縮短
        const float headY = (state_[i] == NoteState::Holding)
                                ? kJudgeY
                                : noteY(n.startTime, songTimeMs, pxPerMs_);
        if (headY < -kNoteH || headY - kNoteH > kScreenH) {
            if (n.endTime <= 0) continue;
        }

        const float x = laneX(n.column) + 4;
        const float w = kLaneW - 8;

        if (n.endTime > 0) {  // 長押身體
            const float tailY = noteY(n.endTime, songTimeMs, pxPerMs_);
            const float top = std::min(headY, tailY);
            const float h = std::abs(headY - tailY) + kNoteH;
            const float alpha = (state_[i] == NoteState::Holding) ? 0.85f : 0.55f;
            DrawRectangleRounded({x, top - kNoteH / 2, w, h}, 0.4f, 4,
                                 Fade(kNoteColors[n.column], alpha));
        }
        DrawRectangleRounded({x, headY - kNoteH / 2, w, kNoteH}, 0.4f, 4,
                             kNoteColors[n.column]);
    }

    // 開場倒數
    if (songTimeMs < 0.0) {
        const int sec = static_cast<int>(std::ceil(-songTimeMs / 1000.0));
        const char* txt = TextFormat("%d", sec);
        const int fs = 120;
        DrawText(txt, kOriginX + kPlayfieldW / 2 - MeasureText(txt, fs) / 2,
                 kScreenH / 2 - fs / 2, fs, Fade(RAYWHITE, 0.6f));
    }

    // ---- HUD ----
    DrawText(TextFormat("SCORE  %08d", score_), 20, 30, 28, RAYWHITE);
    DrawText(TextFormat("COMBO  %d", combo_), 20, 70, 24, combo_ > 0 ? GOLD : GRAY);
    DrawText(TextFormat("MAX    %d", maxCombo_), 20, 100, 20, GRAY);
    DrawText(TextFormat("ACC    %.2f%%", accuracy()), 20, 130, 20, RAYWHITE);

    const Judgment tiers[] = {Judgment::Perfect, Judgment::Great, Judgment::Good,
                              Judgment::Bad, Judgment::Miss};
    int hy = 180;
    for (Judgment t : tiers) {
        DrawText(TextFormat("%-8s %d", judgeName(t), counts_[static_cast<int>(t)]), 20, hy, 20,
                 judgeColor(t));
        hy += 25;
    }

    // 最近一次判定
    if (lastJudge_ != Judgment::None) {
        const char* jtxt = judgeName(lastJudge_);
        const int fs = 40;
        DrawText(jtxt, kOriginX + kPlayfieldW / 2 - MeasureText(jtxt, fs) / 2,
                 static_cast<int>(kJudgeY) - 120, fs, judgeColor(lastJudge_));
    }

    // 鍵位提示
    for (int c = 0; c < kCols; ++c) {
        const int fs = 22;
        std::string k = (settings_.keys[c] == KEY_SPACE)
                            ? "SPC"
                            : std::string(1, static_cast<char>(settings_.keys[c]));
        DrawText(k.c_str(),
                 static_cast<int>(laneX(c)) + kLaneW / 2 - MeasureText(k.c_str(), fs) / 2,
                 static_cast<int>(kJudgeY) + 20, fs, Fade(RAYWHITE, 0.5f));
    }

    DrawFPS(kScreenW - 90, 10);
}

void Game::drawResult() const {
    const int cx = kScreenW / 2;

    DrawText("RESULT", cx - MeasureText("RESULT", 48) / 2, 90, 48, RAYWHITE);
    if (!map_.title.empty()) {
        DrawText(map_.title.c_str(), cx - MeasureText(map_.title.c_str(), 24) / 2, 150, 24,
                 GRAY);
    }

    // 評級
    const char* g = grade();
    Color gcol = (std::string(g) == "SS" || g[0] == 'S') ? GOLD
                 : (g[0] == 'A')                          ? GREEN
                 : (g[0] == 'D')                          ? RED
                                                          : SKYBLUE;
    const int gfs = 160;
    DrawText(g, cx - MeasureText(g, gfs) / 2, 220, gfs, gcol);

    // 左欄：總體數據
    int ly = 430;
    const int fs = 28;
    auto stat = [&](const char* label, const char* value, Color c) {
        DrawText(label, 90, ly, fs, GRAY);
        DrawText(value, 320, ly, fs, c);
        ly += 44;
    };
    stat("ACCURACY", TextFormat("%.2f%%", accuracy()), RAYWHITE);
    stat("SCORE", TextFormat("%d", score_), RAYWHITE);
    stat("MAX COMBO", TextFormat("%d", maxCombo_), GOLD);

    // 右欄：各判定等級次數
    const Judgment tiers[] = {Judgment::Perfect, Judgment::Great, Judgment::Good,
                              Judgment::Bad, Judgment::Miss};
    int ry = 430;
    for (Judgment t : tiers) {
        DrawText(judgeName(t), cx + 120, ry, fs, judgeColor(t));
        const char* v = TextFormat("%d", counts_[static_cast<int>(t)]);
        DrawText(v, kScreenW - 90 - MeasureText(v, fs), ry, fs, judgeColor(t));
        ry += 44;
    }

    // 誤差直方圖
    const int gx = 90, gw = kScreenW - 180, gh = 90;
    const int gy = 640;
    int peak = 1;
    for (int v : hist_) peak = std::max(peak, v);
    DrawRectangle(gx, gy, gw, gh, Color{28, 28, 36, 255});
    DrawLine(gx + gw / 2, gy, gx + gw / 2, gy + gh, Fade(RAYWHITE, 0.3f));  // 0ms 中線
    const float bw = static_cast<float>(gw) / kHistBins;
    for (int i = 0; i < kHistBins; ++i) {
        if (hist_[i] == 0) continue;
        const float h = static_cast<float>(hist_[i]) / peak * gh;
        // 越靠近中央（誤差小）越藍，越外側越紅
        const float off = std::abs(i - kHistBins / 2) / (kHistBins / 2.0f);
        const Color c = off < 0.35f ? SKYBLUE : (off < 0.7f ? GOLD : RED);
        DrawRectangle(static_cast<int>(gx + i * bw), static_cast<int>(gy + gh - h),
                      static_cast<int>(bw) + 1, static_cast<int>(h), c);
    }
    DrawText("early", gx, gy + gh + 6, 18, Fade(RAYWHITE, 0.5f));
    DrawText("late", gx + gw - MeasureText("late", 18), gy + gh + 6, 18, Fade(RAYWHITE, 0.5f));
    DrawText("timing error", cx - MeasureText("timing error", 18) / 2, gy + gh + 6, 18,
             Fade(RAYWHITE, 0.5f));

    // 校正輔助：平均誤差與建議 offset
    if (errSamples_ > 0) {
        const double mean = signedErrSum_ / errSamples_;  // >0 = 偏晚按
        const int suggested = settings_.audioOffsetMs - static_cast<int>(std::lround(mean));
        const char* m = TextFormat("MEAN  %+.1f ms (%s)   suggested offset %+d ms (TAB)", mean,
                                   mean > 0 ? "late" : "early", suggested);
        DrawText(m, cx - MeasureText(m, 22) / 2, gy + gh + 36, 22, Fade(GOLD, 0.85f));
    }

    const char* hint = "Press ENTER to exit";
    DrawText(hint, cx - MeasureText(hint, 24) / 2, kScreenH - 80, 24, Fade(RAYWHITE, 0.6f));
}
