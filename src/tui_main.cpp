#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <raylib.h>

#include "map.h"
#include "play.h"
#include "raii.h"
#include "settings.h"
#include "tui.h"

namespace fs = std::filesystem;
using tui::BrailleCanvas;
using tui::KeyEvent;
using tui::Rgb;
using tui::Terminal;
using Clock = std::chrono::steady_clock;

namespace {

constexpr const char* kConfigFile = "overkey.cfg";
constexpr double kLeadInMs = 2000.0;
constexpr double kBaseApproachMs = 550.0;

const Rgb kLaneColors[7] = {
    {102, 191, 255}, {235, 235, 235}, {102, 191, 255}, {255, 203, 0},
    {102, 191, 255}, {235, 235, 235}, {102, 191, 255},
};
const Rgb kWhite{235, 235, 235};
const Rgb kGray{130, 130, 130};
const Rgb kGold{255, 203, 0};

Rgb judgeRgb(Judgment j) {
    switch (j) {
        case Judgment::Perfect: return {102, 191, 255};
        case Judgment::Great:   return {0, 228, 48};
        case Judgment::Good:    return {255, 203, 0};
        case Judgment::Bad:     return {255, 161, 0};
        case Judgment::Miss:    return {230, 41, 55};
        default:                return kGray;
    }
}

int normKey(int k) { return (k >= 'A' && k <= 'Z') ? k + 32 : k; }

int laneOf(int code, const Settings& s) {
    for (int c = 0; c < 7; ++c)
        if (normKey(s.keys[c]) == normKey(code)) return c;
    return -1;
}

std::string ellipsize(const std::string& s, int maxLen) {
    if (static_cast<int>(s.size()) <= maxLen) return s;
    if (maxLen <= 3) return s.substr(0, maxLen);
    return s.substr(0, maxLen - 3) + "...";
}

// 程式合成短促打擊聲（與 GUI 版相同手法）
Sound makeHitSound() {
    constexpr int sr = 44100;
    constexpr int n = static_cast<int>(sr * 0.05f);
    auto* data = static_cast<short*>(std::malloc(n * sizeof(short)));
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / sr;
        const float env = std::exp(-t * 55.0f);
        const float tone = std::sin(2.0f * PI * 880.0f * t);
        const float noise = (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
        const float v = env * (0.6f * tone + 0.4f * noise * std::exp(-t * 180.0f));
        data[i] = static_cast<short>(std::clamp(v, -1.0f, 1.0f) * 28000.0f);
    }
    const Wave w{static_cast<unsigned>(n), static_cast<unsigned>(sr), 16, 1, data};
    const Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

struct Entry {
    fs::path path;
    std::string label;
};

std::vector<Entry> scanMaps(const fs::path& dir) {
    std::vector<Entry> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const auto& p = it->path();
        if (p.extension() == ".osu" && probeBeatmap(p).isMania7K()) {
            out.push_back({p, p.lexically_relative(dir).replace_extension("").string()});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.label < b.label; });
    return out;
}

constexpr auto kFramePeriod = std::chrono::microseconds(1000000 / 120);  // 目標 120fps

// 回傳 -1 = 離開程式；否則為選擇的 index
int runMenu(Terminal& term, std::vector<Entry>& entries, int& selected) {
    BrailleCanvas canvas(term.cols(), term.rows());
    std::string out;
    std::optional<BeatmapInfo> info;
    int infoFor = -1;

    while (true) {
        term.refreshSize();
        if (canvas.cols() != term.cols() || canvas.rows() != term.rows())
            canvas.resize(term.cols(), term.rows());

        for (const KeyEvent& e : term.poll()) {
            if (e.type != KeyEvent::Press) continue;
            if (e.code == 27 || e.code == 'q' || e.code == 3) return -1;
            if (entries.empty()) continue;
            if (e.code == 'j') selected = (selected + 1) % entries.size();
            if (e.code == 'k')
                selected = (selected + entries.size() - 1) % entries.size();
            if (e.code == 13 || e.code == 32) return selected;
        }

        if (!entries.empty() && selected != infoFor) {
            info = loadBeatmapInfo(entries[selected].path);
            infoFor = selected;
        }

        canvas.clear();
        canvas.putText(2, 1, "OVERKEY  (TUI)", kGold);
        canvas.putText(2, 2, "j/k move   enter play   q quit", kGray);

        if (entries.empty()) {
            canvas.putText(2, 4, "no mania 7K maps found", judgeRgb(Judgment::Miss));
        } else {
            const int top = 4;
            const int visible = term.rows() - top - 2;
            int scroll = std::max(0, selected - visible / 2);
            scroll = std::min(scroll, std::max(0, (int)entries.size() - visible));
            const int listW = term.cols() / 2 - 2;
            for (int i = 0; i < visible && scroll + i < (int)entries.size(); ++i) {
                const int idx = scroll + i;
                const bool sel = (idx == selected);
                std::string line =
                    (sel ? "> " : "  ") + ellipsize(entries[idx].label, listW - 2);
                canvas.putText(2, top + i, line, sel ? kWhite : kGray);
            }
            if (info) {
                const int px = term.cols() / 2 + 2;
                int y = 4;
                canvas.putText(px, y++, ellipsize(info->title, term.cols() - px - 1), kWhite);
                canvas.putText(px, y++, ellipsize(info->artist, term.cols() - px - 1), kGray);
                canvas.putText(px, y++, ellipsize(info->version, term.cols() - px - 1), kGold);
                ++y;
                const int sec = info->lengthMs / 1000;
                canvas.putText(px, y++, "notes  " + std::to_string(info->noteCount), kWhite);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "length %d:%02d", sec / 60, sec % 60);
                canvas.putText(px, y++, buf, kWhite);
            }
        }

        canvas.flush(out);
        term.write(out);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

// 遊玩 + 結算；ESC/q 中途回選單
void playSong(Terminal& term, const Entry& entry, const Settings& settings, Sound hit) {
    Beatmap map = loadBeatmap(entry.path);
    if (map.notes.empty()) return;

    PlaySession session(map.notes);
    const double offset = settings.audioOffsetMs;
    const double approach = kBaseApproachMs / settings.scrollSpeed;

    const fs::path audioPath = entry.path.parent_path() / map.audioFilename;
    MusicRes music{audioPath.string().c_str()};
    const bool haveMusic = music.valid();
    if (haveMusic) SetMusicVolume(music.get(), settings.musicVolume);

    SongClock clock{kLeadInMs};
    bool musicStarted = false;
    double songTimeMs = -kLeadInMs;

    BrailleCanvas canvas(term.cols(), term.rows());
    std::string out;
    auto prev = Clock::now();
    auto nextFrame = prev + kFramePeriod;
    bool playing = true;

    while (true) {
        const auto frameStart = Clock::now();
        const double dt =
            std::chrono::duration<double, std::milli>(frameStart - prev).count();
        prev = frameStart;

        term.refreshSize();
        if (canvas.cols() != term.cols() || canvas.rows() != term.rows())
            canvas.resize(term.cols(), term.rows());

        // 版面（每幀依終端尺寸計算）
        const int laneCells = std::clamp((term.cols() - 4) / 7, 2, 10);
        const int playCells = laneCells * 7;
        const int originCol = (term.cols() - playCells) / 2;
        const int originDotX = originCol * 2;
        const int laneDotW = laneCells * 2;
        const int judgeRow = term.rows() - 3;
        const int judgeDotY = judgeRow * 4;
        const double dotPerMs = judgeDotY / approach;

        // ---- 輸入 ----
        for (const KeyEvent& e : term.poll()) {
            if (e.type == KeyEvent::Press && (e.code == 27 || e.code == 'q' || e.code == 3)) {
                if (musicStarted) StopMusicStream(music.get());
                return;  // 回選單
            }
            if (playing) {
                const int lane = laneOf(e.code, settings);
                if (lane < 0) continue;
                if (e.type == KeyEvent::Press) session.press(lane, songTimeMs);
                else if (e.type == KeyEvent::Release) session.release(lane, songTimeMs);
            } else if (e.type == KeyEvent::Press && (e.code == 13 || e.code == 32)) {
                return;  // 結算畫面 → 回選單
            }
        }

        // ---- 更新 ----
        if (playing) {
            if (musicStarted) UpdateMusicStream(music.get());
            const double audioPos =
                musicStarted ? GetMusicTimePlayed(music.get()) * 1000.0 : -1.0;
            clock.tick(dt, audioPos);
            if (clock.startedThisTick() && haveMusic) {
                PlayMusicStream(music.get());
                musicStarted = true;
            }
            songTimeMs = clock.timeMs() + offset;
            session.advance(songTimeMs);
            bool anyHit = false;
            for (const auto& ev : session.drainEvents()) {
                (void)ev;
                anyHit = true;
            }
            if (anyHit) PlaySound(hit);
            if (session.finished(songTimeMs)) playing = false;
        }

        // ---- 渲染 ----
        canvas.clear();
        if (playing) {
            // 判定線
            canvas.fillDots(originDotX, judgeDotY, originDotX + playCells * 2 - 1, judgeDotY,
                            kWhite);
            // 音符
            const auto& notes = session.notes();
            for (std::size_t i = 0; i < notes.size(); ++i) {
                if (session.stateOf(i) == NoteState::Done) continue;
                const ManiaNote& n = notes[i];
                const bool holding = session.stateOf(i) == NoteState::Holding;
                const int dx0 = originDotX + n.column * laneDotW;
                const int dx1 = dx0 + laneDotW - 1;
                const int headY = holding ? judgeDotY
                                          : judgeDotY - (int)std::lround((n.startTime - songTimeMs) * dotPerMs);
                if (n.endTime > 0) {  // 長押身體
                    const int tailY =
                        judgeDotY - (int)std::lround((n.endTime - songTimeMs) * dotPerMs);
                    canvas.fillDots(dx0, std::min(headY, tailY), dx1, std::max(headY, tailY),
                                    kLaneColors[n.column]);
                }
                if (headY >= 0 && headY <= judgeDotY + 2)
                    canvas.fillDots(dx0, headY - 1, dx1, headY + 1, kLaneColors[n.column]);
            }

            // 倒數
            if (songTimeMs < 0.0) {
                const int sec = (int)std::ceil(-songTimeMs / 1000.0);
                canvas.putText(originCol + playCells / 2 - 1, term.rows() / 2,
                               std::to_string(sec), kWhite);
            }

            // HUD
            char buf[64];
            std::snprintf(buf, sizeof(buf), "SCORE %08d", session.score());
            canvas.putText(1, 0, buf, kWhite);
            std::snprintf(buf, sizeof(buf), "COMBO %d", session.combo());
            canvas.putText(1, 1, buf, session.combo() > 0 ? kGold : kGray);
            std::snprintf(buf, sizeof(buf), "ACC %.2f%%", session.accuracy());
            canvas.putText(1, 2, buf, kWhite);
            if (session.lastJudgment() != Judgment::None) {
                const char* jt = judgeName(session.lastJudgment());
                canvas.putText(originCol + playCells / 2 - (int)std::string(jt).size() / 2,
                               judgeRow - 2, jt, judgeRgb(session.lastJudgment()));
            }
            // 鍵位提示
            for (int c = 0; c < 7; ++c) {
                std::string k = (settings.keys[c] == 32)
                                    ? "SP"
                                    : std::string(1, (char)normKey(settings.keys[c]));
                canvas.putText(originCol + c * laneCells + laneCells / 2, judgeRow + 1, k,
                               kGray);
            }
        } else {
            // 結算
            const int cx = term.cols() / 2;
            canvas.putText(cx - 3, 2, "RESULT", kWhite);
            canvas.putText(cx - 1, 4, session.grade(), kGold);
            char buf[64];
            int y = 6;
            std::snprintf(buf, sizeof(buf), "ACCURACY  %.2f%%", session.accuracy());
            canvas.putText(cx - 12, y++, buf, kWhite);
            std::snprintf(buf, sizeof(buf), "SCORE     %d", session.score());
            canvas.putText(cx - 12, y++, buf, kWhite);
            std::snprintf(buf, sizeof(buf), "MAX COMBO %d", session.maxCombo());
            canvas.putText(cx - 12, y++, buf, kGold);
            ++y;
            const Judgment tiers[] = {Judgment::Perfect, Judgment::Great, Judgment::Good,
                                      Judgment::Bad, Judgment::Miss};
            for (Judgment t : tiers) {
                std::snprintf(buf, sizeof(buf), "%-8s %d", judgeName(t), session.count(t));
                canvas.putText(cx - 12, y++, buf, judgeRgb(t));
            }
            if (session.errSamples() > 0) {
                const double mean = session.meanErrorMs();
                const int sug = settings.audioOffsetMs - (int)std::lround(mean);
                std::snprintf(buf, sizeof(buf), "mean %+.1fms  suggested offset %+dms", mean,
                              sug);
                canvas.putText(cx - 16, ++y, buf, kGold);
            }
            canvas.putText(cx - 10, term.rows() - 2, "enter / esc to return", kGray);
        }

        canvas.flush(out);
        term.write(out);

        // 穩定步調：睡到固定節奏的下一幀（落後則重設，避免雪球）
        std::this_thread::sleep_until(nextFrame);
        nextFrame += kFramePeriod;
        if (nextFrame < Clock::now()) nextFrame = Clock::now() + kFramePeriod;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (!Terminal::isTTY()) {
        std::fprintf(stderr, "overkey-tui 需要在真正的終端機中執行。\n");
        return 1;
    }

    const fs::path mapsDir = (argc > 1) ? argv[1] : "maps";
    Settings settings = loadSettings(kConfigFile);
    std::vector<Entry> entries = scanMaps(mapsDir);

    SetTraceLogLevel(LOG_NONE);  // 避免 raylib 訊息汙染終端機畫面
    InitAudioDevice();
    Sound hit = makeHitSound();
    SetSoundVolume(hit, settings.effectVolume);

    {
        Terminal term;  // RAII：raw mode / alt screen / kitty
        int selected = 0;
        int choice;
        while ((choice = runMenu(term, entries, selected)) >= 0) {
            playSong(term, entries[choice], settings, hit);
        }
    }  // 還原終端機

    UnloadSound(hit);
    CloseAudioDevice();
    return 0;
}
