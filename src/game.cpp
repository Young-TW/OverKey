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

constexpr double kApproachMs = 550.0;                  // 音符自頂端落到判定線的時間
constexpr double kPxPerMs = kJudgeY / kApproachMs;     // 下落速度

constexpr double kPerfectMs = 50.0;                    // |誤差| <= 此值 → Perfect
constexpr double kGoodMs = 120.0;                      // |誤差| <= 此值 → Good，超過即 Miss
constexpr double kLeadInMs = 2000.0;                   // 開場倒數，避免首批音符瞬間出現

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
      judged_(map_.notes.size(), false) {}

void Game::run() {
    RaylibApp app{kScreenW, kScreenH, "OverKey"};
    if (!map_.title.empty()) SetWindowTitle(("OverKey - " + map_.title).c_str());

    MusicRes music{audioPath_.string().c_str()};
    const bool haveMusic = music.valid();

    const double startWall = GetTime() + kLeadInMs / 1000.0;  // 真正 t=0 的牆鐘時間
    bool musicStarted = false;

    while (!WindowShouldClose()) {
        // 時鐘：開場用牆鐘（會由負數倒數到 0）；音樂開始後改以音訊播放位置為準，避免飄移
        double songTimeMs = (GetTime() - startWall) * 1000.0;
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

        BeginDrawing();
        ClearBackground(Color{18, 18, 24, 255});
        draw(songTimeMs);
        EndDrawing();
    }
}

void Game::update(double songTimeMs) {
    // 輸入判定
    for (int c = 0; c < kCols; ++c) {
        if (IsKeyPressed(kKeys[c])) judgePress(c, songTimeMs);
    }

    // 漏接：音符通過判定線超過 Good 視窗仍未判定 → Miss
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        if (judged_[i]) continue;
        if (songTimeMs - map_.notes[i].startTime > kGoodMs) {
            judged_[i] = true;
            combo_ = 0;
            ++misses_;
            lastJudge_ = Judgment::Miss;
        }
    }
}

void Game::judgePress(int column, double songTimeMs) {
    // 找該軌道中最接近現在、且落在 Good 視窗內、尚未判定的音符
    int best = -1;
    double bestAbs = kGoodMs + 1.0;
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        if (judged_[i] || map_.notes[i].column != column) continue;
        const double d = std::abs(map_.notes[i].startTime - songTimeMs);
        if (d <= kGoodMs && d < bestAbs) {
            bestAbs = d;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return;

    judged_[best] = true;
    if (bestAbs <= kPerfectMs) {
        score_ += 300;
        ++perfects_;
        lastJudge_ = Judgment::Perfect;
    } else {
        score_ += 100;
        ++goods_;
        lastJudge_ = Judgment::Good;
    }
    ++combo_;
    maxCombo_ = std::max(maxCombo_, combo_);
}

void Game::draw(double songTimeMs) const {
    // 軌道底色
    for (int c = 0; c < kCols; ++c) {
        Color col = kLaneColors[c];
        if (IsKeyDown(kKeys[c])) {  // 按下時提亮
            col = Color{static_cast<unsigned char>(std::min(255, col.r + 40)),
                        static_cast<unsigned char>(std::min(255, col.g + 40)),
                        static_cast<unsigned char>(std::min(255, col.b + 40)), 255};
        }
        DrawRectangle(static_cast<int>(laneX(c)), 0, kLaneW, kScreenH, col);
    }
    // 軌道分隔線
    for (int c = 0; c <= kCols; ++c) {
        DrawLine(static_cast<int>(laneX(c)), 0, static_cast<int>(laneX(c)), kScreenH,
                 Color{30, 30, 38, 255});
    }

    // 判定線
    DrawRectangle(kOriginX, static_cast<int>(kJudgeY) - 3, kPlayfieldW, 6, RAYWHITE);

    // 音符（含長押尾巴），只畫畫面範圍內、尚未判定的
    for (std::size_t i = 0; i < map_.notes.size(); ++i) {
        if (judged_[i]) continue;
        const ManiaNote& n = map_.notes[i];
        const float y = noteY(n.startTime, songTimeMs);
        if (y < -kNoteH || y - kNoteH > kScreenH) continue;

        const float x = laneX(n.column) + 4;
        const float w = kLaneW - 8;

        if (n.endTime > 0) {  // 長押：從頭畫到尾的長條
            const float tailY = noteY(n.endTime, songTimeMs);
            const float top = std::min(y, tailY);
            const float h = std::abs(y - tailY) + kNoteH;
            DrawRectangleRounded({x, top - kNoteH / 2, w, h}, 0.4f, 4,
                                 Fade(kNoteColors[n.column], 0.55f));
        }
        DrawRectangleRounded({x, y - kNoteH / 2, w, kNoteH}, 0.4f, 4, kNoteColors[n.column]);
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

    DrawText(TextFormat("PERFECT %d", perfects_), 20, 150, 20, SKYBLUE);
    DrawText(TextFormat("GOOD    %d", goods_), 20, 175, 20, GREEN);
    DrawText(TextFormat("MISS    %d", misses_), 20, 200, 20, RED);

    // 最近一次判定（畫在判定線上方中央）
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
