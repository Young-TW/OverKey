#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

#include <raylib.h>

#include "game.h"
#include "play.h"
#include "raii.h"
#include "render.h"

namespace {

// ---- 版面常數 ----
constexpr int kCols = 7;
constexpr int kLaneW = 80;
constexpr int kPlayfieldW = kCols * kLaneW;        // 560
constexpr int kScreenW = kPlayfieldW + 360;        // = kVirtualW
constexpr int kScreenH = 920;                      // = kVirtualH
constexpr int kOriginX = (kScreenW - kPlayfieldW) / 2;
constexpr float kJudgeY = kScreenH - 140.0f;       // 判定線 Y
constexpr float kNoteH = 22.0f;

constexpr double kBaseApproachMs = 550.0;          // scrollSpeed=1 時的下落時間
constexpr double kLeadInMs = 2000.0;               // 開場倒數
constexpr double kFlashDur = 0.18;                 // 命中閃光秒數

constexpr Color kLaneColors[kCols] = {
    {60, 60, 70, 255},  {45, 45, 55, 255},  {60, 60, 70, 255},
    {70, 55, 80, 255},
    {60, 60, 70, 255},  {45, 45, 55, 255},  {60, 60, 70, 255},
};

constexpr Color kNoteColors[kCols] = {
    SKYBLUE, WHITE, SKYBLUE, GOLD, SKYBLUE, WHITE, SKYBLUE,
};

Color judgeColor(Judgment j) {
    switch (j) {
        case Judgment::Perfect: return SKYBLUE;
        case Judgment::Great:   return GREEN;
        case Judgment::Good:    return GOLD;
        case Judgment::Bad:     return ORANGE;
        case Judgment::Miss:    return RED;
        default:                return GRAY;
    }
}

float laneX(int col) { return kOriginX + col * kLaneW; }

// 給定音符時間，回傳此刻頭部中心的 Y
float noteY(int noteTimeMs, double songTimeMs, double pxPerMs) {
    return kJudgeY - static_cast<float>((noteTimeMs - songTimeMs) * pxPerMs);
}

// 程式合成短促打擊聲（正弦 blip + 起音雜訊，快速衰減），免外部素材
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
    UnloadWave(wave);
    return snd;
}

}  // namespace

Game::Game(Beatmap map, std::filesystem::path audioPath, Settings settings)
    : map_(std::move(map)),
      audioPath_(std::move(audioPath)),
      settings_(settings),
      session_(map_.notes),
      offsetMs_(settings.audioOffsetMs),
      approachMs_(kBaseApproachMs / settings.scrollSpeed),
      pxPerMs_(kJudgeY / approachMs_) {
    laneFlash_.fill(-10.0);
}

void Game::run(Viewport& vp) {
    if (!map_.title.empty()) SetWindowTitle(("OverKey - " + map_.title).c_str());

    MusicRes music{audioPath_.string().c_str()};
    const bool haveMusic = music.valid();
    if (haveMusic) SetMusicVolume(music.get(), settings_.musicVolume);

    SoundRes hitSound{makeHitSound()};
    SetSoundVolume(hitSound.get(), settings_.effectVolume);

    SongClock clock{kLeadInMs};
    bool musicStarted = false;
    double songTimeMs = -kLeadInMs;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

        if (phase_ == Phase::Playing) {
            if (IsKeyPressed(KEY_ESCAPE)) {  // 中途放棄，回選單
                if (musicStarted) StopMusicStream(music.get());
                return;
            }

            // 平滑時鐘：每幀以 frame delta 前進，音訊位置只作緩慢校正
            if (musicStarted) UpdateMusicStream(music.get());
            const double audioPos =
                musicStarted ? GetMusicTimePlayed(music.get()) * 1000.0 : -1.0;
            clock.tick(GetFrameTime() * 1000.0, audioPos);
            if (clock.startedThisTick() && haveMusic) {
                PlayMusicStream(music.get());
                musicStarted = true;
            }
            songTimeMs = clock.timeMs() + offsetMs_;

            // 輸入 → core
            for (int c = 0; c < kCols; ++c) {
                if (IsKeyPressed(settings_.keys[c])) session_.press(c, songTimeMs);
                if (IsKeyReleased(settings_.keys[c])) session_.release(c, songTimeMs);
            }
            session_.advance(songTimeMs);

            // core 事件 → 聲光
            bool anyHit = false;
            for (const auto& ev : session_.drainEvents()) {
                triggerFlash(ev.lane, ev.judgment);
                anyHit = true;
            }
            if (anyHit) PlaySound(hitSound.get());

            if (session_.finished(songTimeMs)) {
                phase_ = Phase::Result;
                if (musicStarted) StopMusicStream(music.get());
            }
        } else {  // Result：Enter/Esc 回選單
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                IsKeyPressed(KEY_ESCAPE)) {
                return;
            }
        }

        vp.begin(Color{18, 18, 24, 255});
        if (phase_ == Phase::Playing) {
            drawPlayfield(songTimeMs);
        } else {
            drawResult();
        }
        vp.end();
    }
}

void Game::triggerFlash(int lane, Judgment j) {
    laneFlash_[lane] = GetTime();
    laneFlashJudge_[lane] = j;
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
        DrawRectangleGradientV(x, static_cast<int>(kJudgeY) - 160, kLaneW, 160,
                               Fade(base, 0.0f), Fade(base, 0.35f * a));
        DrawRectangle(x, static_cast<int>(kJudgeY) - 6, kLaneW, 12, Fade(base, 0.8f * a));
    }

    // 判定線
    DrawRectangle(kOriginX, static_cast<int>(kJudgeY) - 3, kPlayfieldW, 6, RAYWHITE);

    // 音符（含長押尾巴），只畫尚未完成、且在畫面範圍內的
    const auto& notes = session_.notes();
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (session_.stateOf(i) == NoteState::Done) continue;
        const ManiaNote& n = notes[i];
        const bool holding = (session_.stateOf(i) == NoteState::Holding);
        const float headY = holding ? kJudgeY : noteY(n.startTime, songTimeMs, pxPerMs_);
        if ((headY < -kNoteH || headY - kNoteH > kScreenH) && n.endTime <= 0) continue;

        const float x = laneX(n.column) + 4;
        const float w = kLaneW - 8;

        if (n.endTime > 0) {  // 長押身體
            const float tailY = noteY(n.endTime, songTimeMs, pxPerMs_);
            const float top = std::min(headY, tailY);
            const float h = std::abs(headY - tailY) + kNoteH;
            DrawRectangleRounded({x, top - kNoteH / 2, w, h}, 0.4f, 4,
                                 Fade(kNoteColors[n.column], holding ? 0.85f : 0.55f));
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
    DrawText(TextFormat("SCORE  %08d", session_.score()), 20, 30, 28, RAYWHITE);
    DrawText(TextFormat("COMBO  %d", session_.combo()), 20, 70, 24,
             session_.combo() > 0 ? GOLD : GRAY);
    DrawText(TextFormat("MAX    %d", session_.maxCombo()), 20, 100, 20, GRAY);
    DrawText(TextFormat("ACC    %.2f%%", session_.accuracy()), 20, 130, 20, RAYWHITE);

    const Judgment tiers[] = {Judgment::Perfect, Judgment::Great, Judgment::Good,
                              Judgment::Bad, Judgment::Miss};
    int hy = 180;
    for (Judgment t : tiers) {
        DrawText(TextFormat("%-8s %d", judgeName(t), session_.count(t)), 20, hy, 20,
                 judgeColor(t));
        hy += 25;
    }

    // 最近一次判定
    if (session_.lastJudgment() != Judgment::None) {
        const char* jtxt = judgeName(session_.lastJudgment());
        const int fs = 40;
        DrawText(jtxt, kOriginX + kPlayfieldW / 2 - MeasureText(jtxt, fs) / 2,
                 static_cast<int>(kJudgeY) - 120, fs, judgeColor(session_.lastJudgment()));
    }

    // 鍵位提示
    for (int c = 0; c < kCols; ++c) {
        const int fs = 22;
        const std::string k = (settings_.keys[c] == KEY_SPACE)
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
    const char* g = session_.grade();
    const Color gcol = (std::string(g) == "SS" || g[0] == 'S') ? GOLD
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
    stat("ACCURACY", TextFormat("%.2f%%", session_.accuracy()), RAYWHITE);
    stat("SCORE", TextFormat("%d", session_.score()), RAYWHITE);
    stat("MAX COMBO", TextFormat("%d", session_.maxCombo()), GOLD);

    // 右欄：各判定等級次數
    const Judgment tiers[] = {Judgment::Perfect, Judgment::Great, Judgment::Good,
                              Judgment::Bad, Judgment::Miss};
    int ry = 430;
    for (Judgment t : tiers) {
        DrawText(judgeName(t), cx + 120, ry, fs, judgeColor(t));
        const char* v = TextFormat("%d", session_.count(t));
        DrawText(v, kScreenW - 90 - MeasureText(v, fs), ry, fs, judgeColor(t));
        ry += 44;
    }

    // 誤差直方圖
    const int gx = 90, gw = kScreenW - 180, gh = 90, gy = 640;
    const auto& hist = session_.histogram();
    int peak = 1;
    for (int v : hist) peak = std::max(peak, v);
    DrawRectangle(gx, gy, gw, gh, Color{28, 28, 36, 255});
    DrawLine(gx + gw / 2, gy, gx + gw / 2, gy + gh, Fade(RAYWHITE, 0.3f));  // 0ms 中線
    const int bins = PlaySession::kHistBins;
    const float bw = static_cast<float>(gw) / bins;
    for (int i = 0; i < bins; ++i) {
        if (hist[i] == 0) continue;
        const float h = static_cast<float>(hist[i]) / peak * gh;
        const float off = std::abs(i - bins / 2) / (bins / 2.0f);
        const Color c = off < 0.35f ? SKYBLUE : (off < 0.7f ? GOLD : RED);
        DrawRectangle(static_cast<int>(gx + i * bw), static_cast<int>(gy + gh - h),
                      static_cast<int>(bw) + 1, static_cast<int>(h), c);
    }
    DrawText("early", gx, gy + gh + 6, 18, Fade(RAYWHITE, 0.5f));
    DrawText("late", gx + gw - MeasureText("late", 18), gy + gh + 6, 18, Fade(RAYWHITE, 0.5f));
    DrawText("timing error", cx - MeasureText("timing error", 18) / 2, gy + gh + 6, 18,
             Fade(RAYWHITE, 0.5f));

    // 校正輔助：平均誤差與建議 offset
    if (session_.errSamples() > 0) {
        const double mean = session_.meanErrorMs();  // >0 = 偏晚按
        const int suggested = settings_.audioOffsetMs - static_cast<int>(std::lround(mean));
        const char* m = TextFormat("MEAN  %+.1f ms (%s)   suggested offset %+d ms (TAB)", mean,
                                   mean > 0 ? "late" : "early", suggested);
        DrawText(m, cx - MeasureText(m, 22) / 2, gy + gh + 36, 22, Fade(GOLD, 0.85f));
    }

    const char* hint = "Press ENTER to exit";
    DrawText(hint, cx - MeasureText(hint, 24) / 2, kScreenH - 80, 24, Fade(RAYWHITE, 0.6f));
}
