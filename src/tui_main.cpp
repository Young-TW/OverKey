#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
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
#include "scores.h"
#include "settings.h"
#include "tui.h"

namespace fs = std::filesystem;
using tui::KeyEvent;
using tui::kKeyDown;
using tui::kKeyF3;
using tui::kKeyF4;
using tui::kKeyLeft;
using tui::kKeyRight;
using tui::kKeyUp;
using tui::PixelCanvas;
using tui::Rgb;
using tui::Terminal;
using Clock = std::chrono::steady_clock;

namespace {

constexpr const char* kConfigFile = "overkey.cfg";
constexpr const char* kScoresFile = "overkey-scores.txt";
constexpr double kSkipLeadMs = 2000.0;  // 跳過前奏後落在第一個音符前多少 ms
constexpr double kMinIntroMs = 3000.0;  // 前奏長於此才可跳過
constexpr double kLeadInMs = 2000.0;
constexpr double kBaseApproachMs = 550.0;

const Rgb kWhite{235, 235, 235};
const Rgb kGray{130, 130, 130};
const Rgb kGold{255, 203, 0};
const Rgb kSky{102, 191, 255};

// 依鍵數產生音軌配色：奇數鍵正中央為金色，其餘藍/白交替
Rgb laneColor(int col, int keyCount) {
    if (keyCount % 2 == 1 && col == keyCount / 2) return kGold;
    return (col % 2 == 0) ? kSky : kWhite;
}

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

int laneOf(int code, const int* keys, int keyCount) {
    for (int c = 0; c < keyCount; ++c)
        if (normKey(keys[c]) == normKey(code)) return c;
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
        if (p.extension() != ".osu" && p.extension() != ".qua") continue;
        const BeatmapHeader h = probeBeatmap(p);
        if (!h.isSupported()) continue;
        std::string label = h.label();
        if (label.empty()) label = p.lexically_relative(dir).replace_extension("").string();
        out.push_back({p, std::move(label)});
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.label < b.label; });
    return out;
}

constexpr auto kFramePeriod = std::chrono::microseconds(1000000 / 1000);  // 目標 1000fps

constexpr int kMenuQuit = -1;
constexpr int kMenuSettings = -2;

constexpr const char* kKittyDeleteAll = "\x1b_Ga=d,d=A,q=2\x1b\\";  // 刪除所有 kitty 圖片
constexpr const char* kKittyClearPlacements =
    "\x1b_Ga=d,d=a,q=2\x1b\\";  // 只刪放置、保留圖片資料

// 為每個軌道傳輸一張圓形 PNG（透明底），image id = lane+1；只傳不顯示（a=t）。
std::string transmitLaneCircles(int keyCount) {
    std::string s;
    for (int c = 0; c < keyCount; ++c) {
        const Rgb lc = laneColor(c, keyCount);
        Image im = GenImageColor(64, 64, Color{0, 0, 0, 0});
        ImageDrawCircle(&im, 32, 32, 31, Color{lc.r, lc.g, lc.b, 255});
        int sz = 0;
        unsigned char* png = ExportImageToMemory(im, ".png", &sz);
        std::string b64 = (png && sz > 0) ? tui::base64Encode(png, (size_t)sz) : "";
        if (png) MemFree(png);
        UnloadImage(im);
        constexpr size_t CH = 4000;
        for (size_t i = 0; i < b64.size(); i += CH) {
            const bool first = (i == 0), last = (i + CH >= b64.size());
            s += "\x1b_G";
            if (first) {
                char h[48];
                std::snprintf(h, sizeof(h), "a=t,f=100,t=d,i=%d,q=2,", c + 1);
                s += h;
            }
            s += last ? "m=0" : "m=1";
            s += ";";
            s += b64.substr(i, CH);
            s += "\x1b\\";
        }
    }
    return s;
}

// 用 kitty graphics protocol 顯示封面：解碼 → PNG → base64 → APC 分塊傳輸並顯示。
// col/row 為 1-based 游標位置；boxCols 為顯示寬（格），outRows 回傳實際高（格，保比例）。
// 回傳要寫入終端機的跳脫序列；失敗回空字串。
std::string buildKittyCover(const fs::path& path, int col, int row, int boxCols, int boxRowsMax,
                            int& outRows) {
    Image img = LoadImage(path.string().c_str());
    if (img.data == nullptr) return "";
    if (img.width > 480) ImageResize(&img, 480, img.height * 480 / img.width);
    const double aspect = static_cast<double>(img.width) / std::max(1, img.height);
    // 格約 1:2（寬:高）→ 保比例的列數
    outRows = std::clamp((int)std::lround(boxCols * 0.5 / aspect), 1, boxRowsMax);

    int sz = 0;
    unsigned char* png = ExportImageToMemory(img, ".png", &sz);
    std::string b64 = (png && sz > 0) ? tui::base64Encode(png, static_cast<size_t>(sz)) : "";
    if (png) MemFree(png);
    UnloadImage(img);
    if (b64.empty()) return "";

    std::string s = kKittyDeleteAll;
    char cur[32];
    std::snprintf(cur, sizeof(cur), "\x1b[%d;%dH", row, col);
    s += cur;

    constexpr size_t CH = 4000;
    for (size_t i = 0; i < b64.size(); i += CH) {
        const bool firstChunk = (i == 0);
        const bool lastChunk = (i + CH >= b64.size());
        s += "\x1b_G";
        if (firstChunk) {
            char hdr[64];
            std::snprintf(hdr, sizeof(hdr), "a=T,f=100,t=d,C=1,q=2,c=%d,r=%d,", boxCols,
                          outRows);
            s += hdr;
        }
        s += lastChunk ? "m=0" : "m=1";
        s += ";";
        s += b64.substr(i, CH);
        s += "\x1b\\";
    }
    return s;
}

// 回傳 kMenuQuit=離開、kMenuSettings=設定，否則為選擇的 index
int runMenu(Terminal& term, std::vector<Entry>& entries, int& selected, float musicVolume,
            const ScoreBook& scores) {
    PixelCanvas canvas(term.cols(), term.rows());
    std::string out;
    std::optional<BeatmapInfo> info;
    int infoFor = -1;

    std::optional<MusicRes> preview;  // hover 副歌試聽
    fs::path previewPath;             // 正在播放的音訊檔（空＝無）
    auto selChangedAt = Clock::now();
    double previewStart = 0.0;
    int lastSel = selected;

    fs::path coverPath;   // 已顯示的封面檔（空＝無）
    int coverRows = 0;    // 封面佔用列數
    const int coverCol = term.cols() / 2 + 2;  // 面板起始欄（1-based）
    const int coverRow = 4;                     // 面板起始列（1-based）

    // 離開選單前刪除 kitty 圖片，避免殘留到遊玩畫面
    auto leave = [&](int r) {
        if (!coverPath.empty()) term.write(kKittyDeleteAll);
        return r;
    };

    while (true) {
        term.refreshSize();
        if (canvas.cols() != term.cols() || canvas.rows() != term.rows())
            canvas.resize(term.cols(), term.rows());

        for (const KeyEvent& e : term.poll()) {
            if (e.type != KeyEvent::Press) continue;
            if (e.code == 27 || e.code == 'q' || e.code == 3) return leave(kMenuQuit);
            if (e.code == 9) return leave(kMenuSettings);  // Tab
            if (entries.empty()) continue;
            if (e.code == 'j' || e.code == kKeyDown)
                selected = (selected + 1) % entries.size();
            if (e.code == 'k' || e.code == kKeyUp)
                selected = (selected + entries.size() - 1) % entries.size();
            if (e.code == 13 || e.code == 32) return leave(selected);
        }
        if (selected != lastSel) {  // 換選取：重啟防抖（先不停試聽）
            lastSel = selected;
            selChangedAt = Clock::now();
        }

        if (!entries.empty() && selected != infoFor) {
            info = loadBeatmapInfo(entries[selected].path);
            infoFor = selected;
        }

        // 副歌試聽：停穩後只有「音訊檔不同」才重載（同曲切難度→續播不重啟）
        if (!entries.empty() && info &&
            std::chrono::duration<double>(Clock::now() - selChangedAt).count() > 0.25) {
            fs::path desired;
            if (!info->audioFilename.empty())
                desired = entries[selected].path.parent_path() / info->audioFilename;
            if (desired != previewPath) {
                preview.reset();
                previewPath = desired;
                if (!desired.empty()) {
                    preview.emplace(desired.string().c_str());
                    if (preview->valid()) {
                        SetMusicVolume(preview->get(), musicVolume);
                        PlayMusicStream(preview->get());
                        previewStart = info->previewTimeMs >= 0
                                           ? info->previewTimeMs / 1000.0
                                           : GetMusicTimeLength(preview->get()) * 0.4;
                        SeekMusicStream(preview->get(), (float)previewStart);
                    } else {
                        previewPath.clear();
                    }
                }
            }
        }
        if (preview && preview->valid()) {
            UpdateMusicStream(preview->get());
            if (GetMusicTimePlayed(preview->get()) >=
                GetMusicTimeLength(preview->get()) - 0.1f)
                SeekMusicStream(preview->get(), (float)previewStart);
        }

        // 封面（kitty graphics）：停穩後只有「檔案不同」才重傳
        if (!entries.empty() && info &&
            std::chrono::duration<double>(Clock::now() - selChangedAt).count() > 0.25) {
            fs::path desired;
            if (!info->backgroundFilename.empty()) {
                const fs::path bg = entries[selected].path.parent_path() / info->backgroundFilename;
                std::error_code ec;
                if (fs::exists(bg, ec)) desired = bg;
            }
            if (desired != coverPath) {
                const int boxCols = std::max(4, term.cols() - coverCol - 1);
                const int boxRowsMax = std::max(3, (term.rows() - coverRow) / 2);
                int rows = 0;
                std::string seq = desired.empty()
                                      ? std::string(kKittyDeleteAll)
                                      : buildKittyCover(desired, coverCol, coverRow, boxCols,
                                                        boxRowsMax, rows);
                if (!desired.empty() && seq.empty()) {  // 載入失敗
                    desired.clear();
                    seq = kKittyDeleteAll;
                    rows = 0;
                }
                term.write(seq);
                coverPath = desired;
                coverRows = rows;
                if (coverRows > 0)
                    canvas.setReserved(coverCol - 1, coverRow - 1, coverCol - 1 + boxCols - 1,
                                       coverRow - 1 + coverRows - 1);
                else
                    canvas.setReserved(0, 0, -1, -1);
                canvas.forceRedraw();  // 保留區變動 → 全畫面重畫
            }
        }

        canvas.clear();
        canvas.putText(2, 1, "OVERKEY  (TUI)", kGold);
        canvas.putText(2, 2, "up/down move   enter play   tab settings   q quit", kGray);

        if (entries.empty()) {
            canvas.putText(2, 4, "no mania 4K/7K maps found", judgeRgb(Judgment::Miss));
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
                // 有封面時文字排在封面下方
                int y = coverRows > 0 ? (coverRow - 1 + coverRows + 1) : 4;
                canvas.putText(px, y++, ellipsize(info->title, term.cols() - px - 1), kWhite);
                canvas.putText(px, y++, ellipsize(info->artist, term.cols() - px - 1), kGray);
                canvas.putText(px, y++, ellipsize(info->version, term.cols() - px - 1), kGold);
                ++y;
                const int sec = info->lengthMs / 1000;
                canvas.putText(px, y++, "notes  " + std::to_string(info->noteCount), kWhite);
                char buf[48];
                std::snprintf(buf, sizeof(buf), "length %d:%02d", sec / 60, sec % 60);
                canvas.putText(px, y++, buf, kWhite);
                ++y;
                const ScoreRecord rec = scores.best(entries[selected].path.string());
                canvas.putText(px, y++, "BEST", kGray);
                if (rec.valid) {
                    std::snprintf(buf, sizeof(buf), "%s  %.2f%%", rec.grade.c_str(),
                                  rec.accuracy);
                    canvas.putText(px, y++, buf, kGold);
                    std::snprintf(buf, sizeof(buf), "%d  x%d", rec.score, rec.maxCombo);
                    canvas.putText(px, y++, buf, kWhite);
                } else {
                    canvas.putText(px, y++, "not played", kGray);
                }
            }
        }

        canvas.flush(out);
        term.write(out);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

// 遊玩 + 結算；ESC/q 中途回選單
void playSong(Terminal& term, const Entry& entry, Settings& settings, Sound hit,
              ScoreBook& scores) {
    Beatmap map = loadBeatmap(entry.path);
    if (map.notes.empty()) return;

    const std::string scoreKey = entry.path.string();
    const ScoreRecord prevBest = scores.best(scoreKey);
    bool recorded = false;  // 是否已提交本局成績

    PlaySession session(map.notes);
    const int keyCount = (map.keyCount == 4) ? 4 : 7;
    const int* laneKeys = (keyCount == 4) ? settings.keys4.data() : settings.keys.data();
    const double offset = settings.audioOffsetMs;
    double approach = kBaseApproachMs / settings.scrollSpeed;

    const fs::path audioPath = entry.path.parent_path() / map.audioFilename;
    MusicRes music{audioPath.string().c_str()};
    const bool haveMusic = music.valid();
    if (haveMusic) SetMusicVolume(music.get(), settings.musicVolume);

    // 譜包自帶打擊取樣優先，否則用傳入的合成音
    std::optional<SoundRes> packHit;
    const fs::path hitPath = findHitSound(entry.path.parent_path());
    if (!hitPath.empty()) {
        packHit.emplace(hitPath.string().c_str());
        if (!packHit->valid()) packHit.reset();
    }
    Sound& hitSound = packHit ? packHit->get() : hit;
    SetSoundVolume(hitSound, settings.effectVolume);

    SongClock clock{kLeadInMs};
    bool musicStarted = false;
    double songTimeMs = -kLeadInMs;

    PixelCanvas canvas(term.cols(), term.rows());
    std::string out;
    auto prev = Clock::now();
    auto nextFrame = prev + kFramePeriod;
    bool playing = true;
    bool paused = false;
    int pauseSel = 0;  // 0 Resume / 1 Retry / 2 Quit

    std::array<Clock::time_point, 7> flashAt{};  // 各軌道最近命中時間
    std::array<Judgment, 7> flashJ{};
    constexpr double kFlashDur = 0.18;

    // 圓形音符 kitty 版（shape==2）：每軌一張圓，開場先傳輸；離開時清除
    if (settings.noteShape == 2) term.write(transmitLaneCircles(keyCount));
    auto cleanupImgs = [&] {
        if (settings.noteShape == 2) term.write(kKittyDeleteAll);
    };

    auto restartAttempt = [&] {
        if (musicStarted) {
            ResumeMusicStream(music.get());  // 若暫停中，先恢復才能正確倒回 0
            StopMusicStream(music.get());
        }
        session = PlaySession(map.notes);
        clock = SongClock{kLeadInMs};
        musicStarted = false;
        songTimeMs = -kLeadInMs;
        playing = true;
        paused = false;
    };

    while (true) {
        const auto frameStart = Clock::now();
        const double dt =
            std::chrono::duration<double, std::milli>(frameStart - prev).count();
        prev = frameStart;

        term.refreshSize();
        if (canvas.cols() != term.cols() || canvas.rows() != term.rows())
            canvas.resize(term.cols(), term.rows());

        // 版面（每幀依終端尺寸計算）。水平單位＝格，垂直單位＝像素(rows*8)
        const int laneCells = std::clamp((term.cols() - 4) / keyCount, 2, 12);
        const int playCells = laneCells * keyCount;
        const int originCol = (term.cols() - playCells) / 2;
        const int judgeRow = term.rows() - 3;
        const int judgePxY = judgeRow * 8;
        const double pxPerMs = judgePxY / approach;

        // ---- 輸入 ----
        for (const KeyEvent& e : term.poll()) {
            const bool press = (e.type == KeyEvent::Press);
            if (press && e.code == 3) {  // Ctrl-C 強制回選單
                if (musicStarted) StopMusicStream(music.get());
                cleanupImgs();
                return;
            }
            if (press && (e.code == '`' || e.code == '~')) {  // 快速重試（任何狀態）
                restartAttempt();
                continue;
            }
            if (press && (e.code == '3' || e.code == '4' || e.code == kKeyF3 ||
                          e.code == kKeyF4)) {  // 速度 3 慢 / 4 快
                const bool faster = (e.code == '4' || e.code == kKeyF4);
                settings.scrollSpeed =
                    std::clamp(settings.scrollSpeed + (faster ? 0.1f : -0.1f), 0.5f, 4.0f);
                approach = kBaseApproachMs / settings.scrollSpeed;
                continue;
            }

            if (paused) {
                if (!press) continue;
                if (e.code == kKeyUp || e.code == 'k') pauseSel = (pauseSel + 2) % 3;
                if (e.code == kKeyDown || e.code == 'j') pauseSel = (pauseSel + 1) % 3;
                if (e.code == 27) {  // Esc 續玩
                    paused = false;
                    if (musicStarted) ResumeMusicStream(music.get());
                } else if (e.code == 13 || e.code == 32) {
                    if (pauseSel == 0) {  // Resume
                        paused = false;
                        if (musicStarted) ResumeMusicStream(music.get());
                    } else if (pauseSel == 1) {  // Retry
                        restartAttempt();
                    } else {  // Quit
                        if (musicStarted) StopMusicStream(music.get());
                        cleanupImgs();
                        return;
                    }
                }
            } else if (playing) {
                if (press && e.code == 27) {  // 開啟暫停選單
                    paused = true;
                    pauseSel = 0;
                    if (musicStarted) PauseMusicStream(music.get());
                    continue;
                }
                // 空白鍵跳過前奏
                const int fn = session.firstNoteMs();
                if (press && e.code == 32 && fn > kMinIntroMs &&
                    songTimeMs < fn - kSkipLeadMs) {
                    const double target = fn - kSkipLeadMs;
                    clock.seek(target);
                    if (haveMusic) {
                        if (!musicStarted) {
                            PlayMusicStream(music.get());
                            musicStarted = true;
                        }
                        SeekMusicStream(music.get(), (float)(target / 1000.0));
                    }
                    songTimeMs = target + offset;
                    continue;
                }
                const int lane = laneOf(e.code, laneKeys, keyCount);
                if (lane < 0) continue;
                if (press) session.press(lane, songTimeMs);
                else if (e.type == KeyEvent::Release) session.release(lane, songTimeMs);
            } else if (press && (e.code == 13 || e.code == 32 || e.code == 27)) {
                cleanupImgs();
                return;  // 結算畫面 → 回選單
            }
        }

        // ---- 更新 ----
        if (playing && !paused) {
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
                flashAt[ev.lane] = frameStart;  // 命中閃光
                flashJ[ev.lane] = ev.judgment;
                anyHit = true;
            }
            if (anyHit) PlaySound(hitSound);
            if (session.finished(songTimeMs)) {
                playing = false;
                if (!recorded) {  // 打完提交成績
                    scores.submit(scoreKey, {session.score(), session.accuracy(),
                                             session.grade(), session.maxCombo(), true});
                    recorded = true;
                }
            }
        }

        // ---- 渲染 ----
        canvas.clear();
        std::string imgOut;  // 本幀圓形音符的 kitty 放置序列
        if (playing) {
            // 命中閃光：判定線上方依判定著色、淡出的亮塊（每軌獨立）
            for (int c = 0; c < keyCount; ++c) {
                const double dt2 =
                    std::chrono::duration<double>(frameStart - flashAt[c]).count();
                if (dt2 < 0.0 || dt2 >= kFlashDur) continue;
                const float a = static_cast<float>(1.0 - dt2 / kFlashDur);
                const Rgb base = judgeRgb(flashJ[c]);
                const Rgb col{static_cast<uint8_t>(18 + (base.r - 18) * a),
                              static_cast<uint8_t>(24 + (base.g - 24) * a),
                              static_cast<uint8_t>(30 + (base.b - 30) * a)};
                const int cx0 = originCol + c * laneCells;
                canvas.fillRect(cx0, judgePxY - 10, cx0 + laneCells - 1, judgePxY - 2, col);
            }
            // 判定線（3 像素粗）
            canvas.fillRect(originCol, judgePxY - 1, originCol + playCells - 1, judgePxY + 1,
                            kWhite);
            // 音符
            const auto& notes = session.notes();
            for (std::size_t i = 0; i < notes.size(); ++i) {
                if (session.stateOf(i) == NoteState::Done) continue;
                const ManiaNote& n = notes[i];
                const bool holding = session.stateOf(i) == NoteState::Holding;
                const int cx0 = originCol + n.column * laneCells;
                const int cx1 = cx0 + laneCells - 1;
                const int headY =
                    holding ? judgePxY
                            : judgePxY - (int)std::lround((n.startTime - songTimeMs) * pxPerMs);
                const Rgb nc = laneColor(n.column, keyCount);
                const int shape = settings.noteShape;

                // Round 1（八分塊）：逐欄依圓方程式算高度，部分格由八分塊平滑；不畫伸進長條那半
                auto circleCap = [&](int centerPx, int half) {
                    const double R = laneCells / 2.0;
                    for (int dx = 0; dx < laneCells; ++dx) {
                        const double ex = (dx + 0.5) - R;
                        if (std::abs(ex) > R) continue;
                        const int halfPx = (int)std::lround(std::sqrt(R * R - ex * ex) * 4.0);
                        int top = centerPx - halfPx, bot = centerPx + halfPx;
                        if (half == 1) top = centerPx;  // 下半
                        if (half == 2) bot = centerPx;  // 上半
                        canvas.fillRect(cx0 + dx, top, cx0 + dx, bot, nc);
                    }
                };
                // Round 2（kitty 圖片，實驗性）：placement id 每顆唯一
                auto place = [&](int centerPx, int placementId) {
                    const int r = std::max(1, laneCells / 2);
                    const int rowTop = centerPx / 8 - r / 2;
                    if (rowTop + r <= 0 || rowTop >= term.rows()) return;
                    char b[112];
                    std::snprintf(b, sizeof(b),
                                  "\x1b[%d;%dH\x1b_Ga=p,i=%d,p=%d,c=%d,r=%d,C=1,q=2\x1b\\",
                                  rowTop + 1, originCol + n.column * laneCells + 1, n.column + 1,
                                  placementId, laneCells, r);
                    imgOut += b;
                };
                const int pid = static_cast<int>(i) + 1;

                if (n.endTime > 0) {  // 長押身體（長條）+ 尾端圓
                    const int tailY =
                        judgePxY - (int)std::lround((n.endTime - songTimeMs) * pxPerMs);
                    canvas.fillRect(cx0, std::min(headY, tailY), cx1, std::max(headY, tailY),
                                    nc);
                    if (shape == 1 && tailY > -64 && tailY < term.rows() * 8 + 64)
                        circleCap(tailY, 2);
                    else if (shape == 2)
                        place(tailY, pid + 1000000);
                }
                if (headY >= 0 && headY <= judgePxY + 4) {
                    if (shape == 1) {
                        circleCap(headY, n.endTime > 0 ? 1 : 0);
                    } else if (shape == 2) {
                        place(headY, pid);
                    } else {
                        // TUI 基準較 GUI 薄，故基準厚度取較大值（每格 8 像素）
                        const int th =
                            std::clamp((int)std::lround(10 * settings.noteScale), 3, 64);
                        canvas.fillRect(cx0, headY - (th - 2), cx1, headY + 1, nc);
                    }
                }
            }

            // 倒數
            if (songTimeMs < 0.0) {
                const int sec = (int)std::ceil(-songTimeMs / 1000.0);
                canvas.putText(originCol + playCells / 2 - 1, term.rows() / 2,
                               std::to_string(sec), kWhite);
            }
            // 跳過前奏提示
            const int fn = session.firstNoteMs();
            if (fn > kMinIntroMs && songTimeMs < fn - kSkipLeadMs) {
                const char* h = "SPACE: skip intro";
                canvas.putText(originCol + playCells / 2 - 8, judgeRow - 3, h, kGray);
            }

            // HUD
            char buf[64];
            std::snprintf(buf, sizeof(buf), "SCORE %08d", session.score());
            canvas.putText(1, 0, buf, kWhite);
            std::snprintf(buf, sizeof(buf), "COMBO %d", session.combo());
            canvas.putText(1, 1, buf, session.combo() > 0 ? kGold : kGray);
            std::snprintf(buf, sizeof(buf), "ACC %.2f%%", session.accuracy());
            canvas.putText(1, 2, buf, kWhite);
            std::snprintf(buf, sizeof(buf), "SPEED %.1fx %.0fms", settings.scrollSpeed,
                          (term.rows() * 8) / pxPerMs);
            canvas.putText(1, 3, buf, kGray);
            if (session.lastJudgment() != Judgment::None) {
                const char* jt = judgeName(session.lastJudgment());
                canvas.putText(originCol + playCells / 2 - (int)std::string(jt).size() / 2,
                               judgeRow - 2, jt, judgeRgb(session.lastJudgment()));
            }
            // 鍵位提示
            for (int c = 0; c < keyCount; ++c) {
                std::string k = (laneKeys[c] == 32)
                                    ? "SP"
                                    : std::string(1, (char)normKey(laneKeys[c]));
                canvas.putText(originCol + c * laneCells + laneCells / 2, judgeRow + 1, k,
                               kGray);
            }
            // 暫停選單（疊在凍結畫面上）
            if (paused) {
                const int cx = term.cols() / 2;
                const int my = term.rows() / 2 - 2;
                canvas.putText(cx - 3, my - 2, "PAUSED", kWhite);
                const char* items[] = {"Resume", "Retry", "Quit"};
                for (int i = 0; i < 3; ++i) {
                    const bool sel = (i == pauseSel);
                    const std::string line =
                        (sel ? "> " : "  ") + std::string(items[i]);
                    canvas.putText(cx - 4, my + i, line, sel ? kGold : kGray);
                }
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
            ++y;
            if (prevBest.valid && session.score() > prevBest.score) {
                canvas.putText(cx - 6, y, "NEW RECORD!", kGold);
            } else if (prevBest.valid) {
                std::snprintf(buf, sizeof(buf), "BEST  %d  %.2f%%  %s", prevBest.score,
                              prevBest.accuracy, prevBest.grade.c_str());
                canvas.putText(cx - 14, y, buf, kGray);
            }
            canvas.putText(cx - 10, term.rows() - 2, "enter / esc to return", kGray);
        }

        canvas.flush(out);
        term.write(out);

        // 圓形音符 kitty 版：先清掉上一幀的放置，再放本幀（結算畫面則清空不放）
        if (settings.noteShape == 2) {
            term.write(kKittyClearPlacements);
            if (playing) term.write(imgOut);
        }

        // 穩定步調：睡到固定節奏的下一幀（落後則重設，避免雪球）
        std::this_thread::sleep_until(nextFrame);
        nextFrame += kFramePeriod;
        if (nextFrame < Clock::now()) nextFrame = Clock::now() + kFramePeriod;
    }
}

std::string keyLabel(int code) {
    if (code == 32) return "SPACE";
    if (code >= 33 && code <= 126) return std::string(1, (char)code);
    return "#" + std::to_string(code);
}

// 字母鍵改存大寫，與 GUI（raylib keycode）一致
int toStoredKey(int kittyCode) {
    return (kittyCode >= 'a' && kittyCode <= 'z') ? kittyCode - 32 : kittyCode;
}

// 設定畫面：方向鍵/jk 選欄位、左右/hl 調整、enter 重綁鍵、esc 存檔返回
void runSettings(Terminal& term, Settings& settings) {
    PixelCanvas canvas(term.cols(), term.rows());
    std::string out;
    constexpr int k7kBase = 6;             // 前 6 項為數值欄位
    constexpr int k4kBase = k7kBase + 7;   // 13
    constexpr int kFields = k4kBase + 4;   // 17
    int selected = 0;
    int rebinding = -1;
    // 欄位索引 → 對應 keybind 槽（nullptr 表非鍵位欄位）
    auto keySlot = [&](int field) -> int* {
        if (field >= k7kBase && field < k4kBase) return &settings.keys[field - k7kBase];
        if (field >= k4kBase && field < kFields) return &settings.keys4[field - k4kBase];
        return nullptr;
    };
    auto next = Clock::now();

    while (true) {
        term.refreshSize();
        if (canvas.cols() != term.cols() || canvas.rows() != term.rows())
            canvas.resize(term.cols(), term.rows());

        for (const KeyEvent& e : term.poll()) {
            if (e.type != KeyEvent::Press) continue;
            if (rebinding >= 0) {
                if (e.code == 27) {
                    rebinding = -1;  // 取消
                } else if (e.code >= 32 && e.code <= 126) {  // 僅接受可列印鍵
                    if (int* slot = keySlot(rebinding)) *slot = toStoredKey(e.code);
                    rebinding = -1;
                }
                continue;
            }
            if (e.code == 27 || e.code == 'q' || e.code == 9) return;  // 存檔由呼叫端負責
            if (e.code == 'j' || e.code == kKeyDown) selected = (selected + 1) % kFields;
            if (e.code == 'k' || e.code == kKeyUp)
                selected = (selected + kFields - 1) % kFields;
            const int dir = (e.code == kKeyRight || e.code == 'l') ? 1
                            : (e.code == kKeyLeft || e.code == 'h') ? -1
                                                                    : 0;
            if (selected == 0 && dir)
                settings.audioOffsetMs = std::clamp(settings.audioOffsetMs + dir * 5, -300, 300);
            else if (selected == 1 && dir)
                settings.scrollSpeed = std::clamp(settings.scrollSpeed + dir * 0.1f, 0.5f, 4.0f);
            else if (selected == 2 && dir)
                settings.musicVolume = std::clamp(settings.musicVolume + dir * 0.05f, 0.0f, 1.0f);
            else if (selected == 3 && dir)
                settings.effectVolume = std::clamp(settings.effectVolume + dir * 0.05f, 0.0f, 1.0f);
            else if (selected == 4 && dir)
                settings.noteScale = std::clamp(settings.noteScale + dir * 0.1f, 0.5f, 3.0f);
            else if (selected == 5 && dir)
                settings.noteShape = (settings.noteShape + (dir > 0 ? 1 : 2)) % 3;
            else if (selected >= k7kBase && (e.code == 13 || e.code == 32))
                rebinding = selected;
        }

        canvas.clear();
        canvas.putText(2, 1, "SETTINGS", kGold);
        char buf[64];
        auto row = [&](int idx, const char* label, const std::string& val, int y) {
            const bool sel = (idx == selected);
            const Rgb lc = sel ? kWhite : kGray;
            canvas.putText(4, y, (sel ? "> " : "  ") + std::string(label), lc);
            canvas.putText(28, y, val, sel ? kGold : kWhite);
        };
        int y = 3;
        std::snprintf(buf, sizeof(buf), "%+d ms", settings.audioOffsetMs);
        row(0, "Audio offset", buf, y++);
        std::snprintf(buf, sizeof(buf), "%.1fx", settings.scrollSpeed);
        row(1, "Scroll speed", buf, y++);
        std::snprintf(buf, sizeof(buf), "%d%%", (int)(settings.musicVolume * 100));
        row(2, "Music volume", buf, y++);
        std::snprintf(buf, sizeof(buf), "%d%%", (int)(settings.effectVolume * 100));
        row(3, "Effect volume", buf, y++);
        std::snprintf(buf, sizeof(buf), "%.1fx", settings.noteScale);
        row(4, "Note height", buf, y++);
        const char* shapeName[] = {"Bar", "Round", "Round (kitty*)"};
        row(5, "Note shape", shapeName[std::clamp(settings.noteShape, 0, 2)], y++);
        ++y;
        canvas.putText(4, y++, "7K KEYBINDS", kGray);
        for (int i = 0; i < 7; ++i) {
            std::snprintf(buf, sizeof(buf), "Lane %d", i + 1);
            const std::string val =
                (rebinding == k7kBase + i) ? "press a key..." : keyLabel(settings.keys[i]);
            row(k7kBase + i, buf, val, y++);
        }
        ++y;
        canvas.putText(4, y++, "4K KEYBINDS", kGray);
        for (int i = 0; i < 4; ++i) {
            std::snprintf(buf, sizeof(buf), "Lane %d", i + 1);
            const std::string val =
                (rebinding == k4kBase + i) ? "press a key..." : keyLabel(settings.keys4[i]);
            row(k4kBase + i, buf, val, y++);
        }
        canvas.putText(4, term.rows() - 2,
                       "up/down field   left/right adjust   enter rebind   esc save", kGray);

        canvas.flush(out);
        term.write(out);
        next += std::chrono::milliseconds(16);
        std::this_thread::sleep_until(next);
        if (next < Clock::now()) next = Clock::now();
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
    ScoreBook scores{kScoresFile};
    std::vector<Entry> entries = scanMaps(mapsDir);

    SetTraceLogLevel(LOG_NONE);  // 避免 raylib 訊息汙染終端機畫面
    InitAudioDevice();
    Sound hit = makeHitSound();
    SetSoundVolume(hit, settings.effectVolume);

    {
        Terminal term;  // RAII：raw mode / alt screen / kitty
        int selected = 0;
        while (true) {
            const int choice = runMenu(term, entries, selected, settings.musicVolume, scores);
            if (choice == kMenuQuit) break;
            if (choice == kMenuSettings) {
                runSettings(term, settings);
                saveSettings(settings, kConfigFile);
                SetSoundVolume(hit, settings.effectVolume);  // 套用新音效音量
                continue;
            }
            const float prevSpeed = settings.scrollSpeed;
            playSong(term, entries[choice], settings, hit, scores);
            if (settings.scrollSpeed != prevSpeed)  // F3/F4 調過則存回
                saveSettings(settings, kConfigFile);
        }
    }  // 還原終端機

    UnloadSound(hit);
    CloseAudioDevice();
    return 0;
}
