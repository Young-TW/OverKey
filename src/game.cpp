#include <algorithm>
#include <cmath>
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

constexpr double kApproachMs = 550.0;                  // 音符自頂端落到判定線的時間
constexpr double kPxPerMs = kJudgeY / kApproachMs;     // 下落速度

constexpr double kPerfectMs = 50.0;                    // |誤差| <= 此值 → Perfect
constexpr double kGoodMs = 120.0;                      // |誤差| <= 此值 → Good，超過即 Miss
constexpr double kLeadInMs = 2000.0;                   // 開場倒數，避免首批音符瞬間出現
constexpr double kEndPadMs = 1500.0;                   // 最後音符後到結算的餘量

constexpr int kPerfectPts = 300;
constexpr int kGoodPts = 100;

// 7K 預設鍵位：S D F [Space] J K L
constexpr int kKeys[kCols] = {
    KEY_S, KEY_D, KEY_F, KEY_SPACE, KEY_J, KEY_K, KEY_L,
};

constexpr Color kLaneColors[kCols] = {
    {60, 60, 70, 255},  {45, 45, 55, 255},  {60, 60, 70, 255},
    {70, 55, 80, 255},
    {60, 60, 70, 255},  {45, 45, 55, 255},  {60, 60, 70, 255},
};

constexpr Color kNoteColors[kCols] = {
    SKYBLUE, WHITE, SKYBLUE, GOLD, SKYBLUE, WHITE, SKYBLUE,
};

float laneX(int col) { return kOriginX + col * kLaneW; }

// 給定音符時間，回傳此刻其頭部中心應在的 Y
float noteY(int noteTimeMs, double songTimeMs) {
    return kJudgeY - static_cast<float>((noteTimeMs - songTimeMs) * kPxPerMs);
}

}  // namespace

Game::Game(Beatmap map, std::filesystem::path audioPath)
    : map_(std::move(map)),
      audioPath_(std::move(audioPath)),
      state_(map_.notes.size(), NoteState::Idle) {
    holding_.fill(-1);
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

            update(songTimeMs);

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
        if (IsKeyPressed(kKeys[c])) judgePress(c, songTimeMs);
        if (IsKeyReleased(kKeys[c])) judgeRelease(c, songTimeMs);
    }

    // 逐音符推進狀態
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        const ManiaNote& n = map_.notes[i];
        switch (state_[i]) {
            case NoteState::Idle:
                // 頭部通過判定線超過 Good 視窗仍未按 → Miss（長押則頭尾都算漏）
                if (songTimeMs - n.startTime > kGoodMs) {
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
                }
                break;
            case NoteState::Done:
                break;
        }
    }
}

void Game::judgePress(int column, double songTimeMs) {
    // 找該軌道中最接近現在、落在 Good 視窗內、尚未判定的音符頭
    int best = -1;
    double bestAbs = kGoodMs + 1.0;
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        if (state_[i] != NoteState::Idle || map_.notes[i].column != column) continue;
        const double d = std::abs(map_.notes[i].startTime - songTimeMs);
        if (d <= kGoodMs && d < bestAbs) {
            bestAbs = d;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return;

    addJudgment(judgeByError(bestAbs));  // 頭部判定
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

    const double err = std::abs(songTimeMs - n.endTime);
    if (songTimeMs < n.endTime - kGoodMs) {
        addJudgment(Judgment::Miss);  // 提早放開
    } else {
        addJudgment(judgeByError(err));  // 在尾端視窗內放開
    }
}

Game::Judgment Game::judgeByError(double absErrMs) const {
    if (absErrMs <= kPerfectMs) return Judgment::Perfect;
    if (absErrMs <= kGoodMs) return Judgment::Good;
    return Judgment::Miss;
}

void Game::addJudgment(Judgment j) {
    lastJudge_ = j;
    switch (j) {
        case Judgment::Perfect:
            score_ += kPerfectPts;
            pointsAccum_ += kPerfectPts;
            ++perfects_;
            ++combo_;
            break;
        case Judgment::Good:
            score_ += kGoodPts;
            pointsAccum_ += kGoodPts;
            ++goods_;
            ++combo_;
            break;
        case Judgment::Miss:
            ++misses_;
            combo_ = 0;
            break;
        case Judgment::None:
            return;
    }
    maxCombo_ = std::max(maxCombo_, combo_);
}

double Game::accuracy() const {
    if (totalUnits_ == 0) return 0.0;
    return static_cast<double>(pointsAccum_) / (totalUnits_ * kPerfectPts) * 100.0;
}

const char* Game::grade() const {
    if (misses_ == 0 && goods_ == 0 && perfects_ > 0) return "SS";
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
        if (IsKeyDown(kKeys[c])) {
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

    // 判定線
    DrawRectangle(kOriginX, static_cast<int>(kJudgeY) - 3, kPlayfieldW, 6, RAYWHITE);

    // 音符（含長押尾巴），只畫尚未完成、且在畫面範圍內的
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        if (state_[i] == NoteState::Done) continue;
        const ManiaNote& n = map_.notes[i];
        // 按住中的長押：頭部固定在判定線，身體隨尾端縮短
        const float headY = (state_[i] == NoteState::Holding)
                                ? kJudgeY
                                : noteY(n.startTime, songTimeMs);
        if (headY < -kNoteH || headY - kNoteH > kScreenH) {
            if (n.endTime <= 0) continue;
        }

        const float x = laneX(n.column) + 4;
        const float w = kLaneW - 8;

        if (n.endTime > 0) {  // 長押身體
            const float tailY = noteY(n.endTime, songTimeMs);
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

    DrawText(TextFormat("PERFECT %d", perfects_), 20, 180, 20, SKYBLUE);
    DrawText(TextFormat("GOOD    %d", goods_), 20, 205, 20, GREEN);
    DrawText(TextFormat("MISS    %d", misses_), 20, 230, 20, RED);

    // 最近一次判定
    const char* jtxt = nullptr;
    Color jcol = RAYWHITE;
    switch (lastJudge_) {
        case Judgment::Perfect: jtxt = "PERFECT"; jcol = SKYBLUE; break;
        case Judgment::Good:    jtxt = "GOOD";    jcol = GREEN;   break;
        case Judgment::Miss:    jtxt = "MISS";    jcol = RED;     break;
        case Judgment::None:    break;
    }
    if (jtxt) {
        const int fs = 40;
        DrawText(jtxt, kOriginX + kPlayfieldW / 2 - MeasureText(jtxt, fs) / 2,
                 static_cast<int>(kJudgeY) - 120, fs, jcol);
    }

    // 鍵位提示
    const char* keys[kCols] = {"S", "D", "F", "SPC", "J", "K", "L"};
    for (int c = 0; c < kCols; ++c) {
        const int fs = 24;
        DrawText(keys[c],
                 static_cast<int>(laneX(c)) + kLaneW / 2 - MeasureText(keys[c], fs) / 2,
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

    // 數據
    int y = 430;
    const int fs = 30;
    auto row = [&](const char* label, const char* value, Color c) {
        DrawText(label, cx - 200, y, fs, GRAY);
        DrawText(value, cx + 60, y, fs, c);
        y += 48;
    };
    row("ACCURACY", TextFormat("%.2f%%", accuracy()), RAYWHITE);
    row("SCORE", TextFormat("%d", score_), RAYWHITE);
    row("MAX COMBO", TextFormat("%d", maxCombo_), GOLD);
    row("PERFECT", TextFormat("%d", perfects_), SKYBLUE);
    row("GOOD", TextFormat("%d", goods_), GREEN);
    row("MISS", TextFormat("%d", misses_), RED);

    const char* hint = "Press ENTER to exit";
    DrawText(hint, cx - MeasureText(hint, 24) / 2, kScreenH - 80, 24, Fade(RAYWHITE, 0.6f));
}
