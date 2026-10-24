// 前端無關 core 的單元測試：map 解析、PlaySession 判定/計分、SongClock。
// 不依賴 raylib 視窗/音訊，可 headless 執行。極簡 assert 框架。
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "library.h"
#include "map.h"
#include "play.h"
#include "pp.h"
#include "scores.h"
#include "settings.h"
#include "strutil.h"

namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                        \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                       \
    } while (0)

namespace {

// 在暫存目錄寫一張 .osu，回傳路徑
fs::path writeOsu(const std::string& name, int mode, int keys, int previewTime,
                  const std::string& hitObjects) {
    const fs::path dir = fs::temp_directory_path() / "overkey_test";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    std::ofstream f(p, std::ios::trunc);
    f << "osu file format v14\n\n"
      << "[General]\nAudioFilename: audio.mp3\nMode: " << mode
      << "\nPreviewTime: " << previewTime << "\n\n"
      << "[Metadata]\nTitle:T\nArtist:A\nVersion:V\n\n"
      << "[Difficulty]\nCircleSize:" << keys << "\n\n"
      << "[HitObjects]\n"
      << hitObjects;
    return p;
}

// 7K 各軌道中心 x，使 (x*keys)/512 還原成 column
int colX(int col, int keys) { return (col * 512 + 256) / keys; }

void testMapParsing() {
    std::printf("map parsing\n");
    // 7K：四個普通音符 + 一個長押
    std::string ho;
    ho += std::to_string(colX(0, 7)) + ",192,1000,1,0,0:0:0:0:\n";
    ho += std::to_string(colX(6, 7)) + ",192,1200,1,0,0:0:0:0:\n";
    ho += std::to_string(colX(3, 7)) + ",192,1400,128,0,1800:0:0:0:0:\n";  // 長押 1400→1800
    const fs::path p = writeOsu("7k.osu", 3, 7, 1500, ho);

    const Beatmap bm = loadBeatmap(p);
    CHECK(bm.keyCount == 7);
    CHECK(bm.notes.size() == 3);
    CHECK(bm.notes[0].column == 0);
    CHECK(bm.notes[1].column == 6);              // 邊界不越界
    CHECK(bm.notes[2].column == 3);
    CHECK(bm.notes[0].endTime == -1);            // 普通音符
    CHECK(bm.notes[2].endTime == 1800);          // 長押結束時間正確（去掉 hitsample）

    const BeatmapInfo info = loadBeatmapInfo(p);
    CHECK(info.mode == 3);
    CHECK(info.keyCount == 7);
    CHECK(info.noteCount == 3);
    CHECK(info.lengthMs == 1400);                // 輕量解析只看起始時間（不含長押尾）
    CHECK(info.previewTimeMs == 1500);
    CHECK(info.audioFilename == "audio.mp3");

    const BeatmapHeader h = probeBeatmap(p);
    CHECK(h.mode == 3 && h.keyCount == 7);
    CHECK(h.isSupported());

    // 4K column 對應
    std::string ho4 = std::to_string(colX(0, 4)) + ",192,500,1,0,0:0:0:0:\n" +
                      std::to_string(colX(3, 4)) + ",192,600,1,0,0:0:0:0:\n";
    const Beatmap bm4 = loadBeatmap(writeOsu("4k.osu", 3, 4, -1, ho4));
    CHECK(bm4.keyCount == 4);
    CHECK(bm4.notes[0].column == 0);
    CHECK(bm4.notes[1].column == 3);

    // 篩選：4K–7K 支援；非 mania、支援範圍外鍵數不支援
    CHECK(probeBeatmap(writeOsu("4k2.osu", 3, 4, -1, ho4)).isSupported());
    CHECK(probeBeatmap(writeOsu("5k.osu", 3, 5, -1, ho4)).isSupported());     // 5K 支援
    CHECK(probeBeatmap(writeOsu("6k.osu", 3, 6, -1, ho4)).isSupported());     // 6K 支援
    CHECK(!probeBeatmap(writeOsu("std.osu", 0, 5, -1, ho4)).isSupported());   // 非 mania
    CHECK(!probeBeatmap(writeOsu("8k.osu", 3, 8, -1, ho4)).isSupported());    // 8K 未支援

    // findHitSound：無取樣→空；有則找到並依優先序
    const fs::path dir = p.parent_path();
    CHECK(findHitSound(dir).empty());
    { std::ofstream(dir / "drum-hitnormal.ogg") << "x"; }
    CHECK(findHitSound(dir) == dir / "drum-hitnormal.ogg");
    { std::ofstream(dir / "normal-hitnormal.wav") << "x"; }
    CHECK(findHitSound(dir) == dir / "normal-hitnormal.wav");  // normal 優先於 drum
}

void testQuaParsing() {
    std::printf("quaver .qua parsing\n");
    const fs::path dir = fs::temp_directory_path() / "overkey_test";
    fs::create_directories(dir);
    const fs::path p = dir / "test.qua";
    {
        std::ofstream f(p, std::ios::trunc);
        f << "AudioFile: song.mp3\n"
          << "SongPreviewTime: 4200\n"
          << "Mode: Keys4\n"
          << "Title: MyTitle\n"
          << "Artist: MyArtist\n"
          << "DifficultyName: Insane\n"
          << "TimingPoints:\n- StartTime: 0\n  Bpm: 120\n"
          << "HitObjects:\n"
          << "- StartTime: 1000\n  Lane: 1\n"
          << "- StartTime: 1200\n  Lane: 4\n"
          << "- StartTime: 1400\n  Lane: 2\n  EndTime: 1800\n";  // 長押
    }
    const Beatmap bm = loadBeatmap(p);
    CHECK(bm.keyCount == 4);
    CHECK(bm.audioFilename == "song.mp3");
    CHECK(bm.notes.size() == 3);
    CHECK(bm.notes[0].column == 0);   // Lane 1 → column 0
    CHECK(bm.notes[1].column == 3);   // Lane 4 → column 3
    CHECK(bm.notes[2].column == 1 && bm.notes[2].endTime == 1800);

    const BeatmapInfo info = loadBeatmapInfo(p);
    CHECK(info.mode == 3 && info.keyCount == 4);
    CHECK(info.title == "MyTitle" && info.artist == "MyArtist" && info.version == "Insane");
    CHECK(info.noteCount == 3);
    CHECK(info.lengthMs == 1800);       // 長押尾
    CHECK(info.previewTimeMs == 4200);
    CHECK(info.audioFilename == "song.mp3");

    const BeatmapHeader h = probeBeatmap(p);
    CHECK(h.mode == 3 && h.keyCount == 4 && h.isSupported());
}

// 單一普通音符，測不同按壓時間的判定等級
Judgment judgeTapAt(int noteTime, double pressTime) {
    PlaySession s(std::vector<ManiaNote>{{0, noteTime, -1}});
    s.press(0, pressTime);
    auto ev = s.drainEvents();
    if (ev.empty()) return Judgment::Miss;  // 視窗外不產生命中事件
    return ev[0].judgment;
}

void testJudgmentTiers() {
    std::printf("judgment tiers\n");
    CHECK(judgeTapAt(1000, 1000) == Judgment::Perfect);   // 0ms
    CHECK(judgeTapAt(1000, 1030) == Judgment::Perfect);   // 30ms ≤35
    CHECK(judgeTapAt(1000, 1060) == Judgment::Great);     // 60ms ≤65
    CHECK(judgeTapAt(1000, 1090) == Judgment::Good);      // 90ms ≤95
    CHECK(judgeTapAt(1000, 1120) == Judgment::Bad);       // 120ms ≤125
    CHECK(judgeTapAt(1000, 1300) == Judgment::Miss);      // 視窗外：不命中
    CHECK(judgeTapAt(1000, 970) == Judgment::Perfect);    // 早按 30ms
    CHECK(judgeTapAt(1000, 940) == Judgment::Great);      // 早按 60ms：對稱為 Great
}

// 命中事件攜帶的有號誤差（前端誤差條用）：>0 = 偏晚、<0 = 偏早
void testHitEventError() {
    std::printf("hit event signed error\n");
    {
        PlaySession s(std::vector<ManiaNote>{{0, 1000, -1}});
        s.press(0, 1030);
        const auto ev = s.drainEvents();
        CHECK(ev.size() == 1 && ev[0].errMs == 30.0);   // 晚按 30ms
    }
    {
        PlaySession s(std::vector<ManiaNote>{{0, 1000, -1}});
        s.press(0, 970);
        const auto ev = s.drainEvents();
        CHECK(ev.size() == 1 && ev[0].errMs == -30.0);  // 早按 30ms
    }
}

void testScoringAndMiss() {
    std::printf("scoring / miss / combo\n");
    PlaySession s(std::vector<ManiaNote>{{0, 1000, -1}, {1, 1000, -1}, {2, 1000, -1}});
    s.press(0, 1000);  // Perfect
    s.press(1, 1000);  // Perfect
    CHECK(s.combo() == 2);
    CHECK(s.count(Judgment::Perfect) == 2);
    CHECK(s.score() == 600);

    // 第三個不按，過了視窗 → advance 判 Miss、斷連段
    s.advance(2000);
    CHECK(s.count(Judgment::Miss) == 1);
    CHECK(s.combo() == 0);
    CHECK(s.maxCombo() == 2);

    // accuracy：2×300 / (3×300) = 66.67%
    CHECK(s.accuracy() > 66.0 && s.accuracy() < 67.0);

    // 全 Perfect → SS
    PlaySession s2(std::vector<ManiaNote>{{0, 100, -1}});
    s2.press(0, 100);
    CHECK(std::string(s2.grade()) == "SS");
    CHECK(s2.finished(100 + 2000));  // songEnd = last + padding
}

void testLongNote() {
    std::printf("long note\n");
    // 長押 1000→1500，頭尾各一個判定單位（共 2 unit）
    PlaySession s(std::vector<ManiaNote>{{0, 1000, 1500}});
    s.press(0, 1000);                       // 頭 Perfect
    CHECK(s.count(Judgment::Perfect) == 1);
    s.advance(1500);                        // 按住到尾 → 尾 Perfect
    CHECK(s.count(Judgment::Perfect) == 2);

    // 提早放開 → 尾 Miss
    PlaySession s2(std::vector<ManiaNote>{{0, 1000, 1500}});
    s2.press(0, 1000);
    s2.release(0, 1200);                    // 早放（距尾 >125ms）
    CHECK(s2.count(Judgment::Miss) == 1);
    CHECK(s2.combo() == 0);
}

void testSongClock() {
    std::printf("song clock\n");
    SongClock c(2000.0);                    // 2s 倒數
    CHECK(c.timeMs() == -2000.0);
    CHECK(!c.running());
    // 推進到 0
    for (int i = 0; i < 200; ++i) c.tick(10.0, -1.0);  // 200×10ms = 2000ms
    CHECK(c.running());
    CHECK(c.timeMs() >= 0.0 && c.timeMs() < 20.0);

    // 等速前進：無音訊時純 frame-delta 累積
    const double before = c.timeMs();
    c.tick(16.0, -1.0);
    CHECK(c.timeMs() > before + 15.0 && c.timeMs() < before + 17.0);

    // 大幅偏離（>100ms）→ 硬對齊音訊位置
    c.tick(16.0, 50000.0);
    CHECK(c.timeMs() == 50000.0);

    // 小偏差（≤100ms）→ 維持等速、不被拉走（避免抖動）
    const double t1 = c.timeMs();
    c.tick(16.0, t1 + 50.0);                // 音訊只快 50ms
    CHECK(c.timeMs() < t1 + 20.0);          // 仍約 +16ms，沒跳到 +50
}

// 播完音樂後音訊位置不可信（繞回 0 / 凍結檔尾），時鐘不能被釘住——
// 這是「譜面播完卡在遊戲畫面進不了結算」的回歸測試。
void testSongClockPastAudioEnd() {
    std::printf("song clock past audio end\n");

    // (a) 游標凍結在檔尾（TUI/miniaudio）：時鐘必須繼續前進越過結尾
    {
        SongClock c(0.0);
        c.seek(9800.0);
        double audio = 9800.0;
        for (int i = 0; i < 20; ++i) { audio += 10.0; c.tick(10.0, audio); }  // 音訊正常推進
        for (int i = 0; i < 50; ++i) c.tick(20.0, audio);                     // 之後凍結不動
        CHECK(c.timeMs() > audio + 500.0);  // 舊實作會被釘在 audio+~100ms
    }

    // (b) 音樂繞回開頭（GUI/raylib 預設循環播放）：大幅回跳必須忽略
    {
        SongClock c(0.0);
        c.seek(50000.0);
        double audio = 50000.0;
        for (int i = 0; i < 10; ++i) { audio += 10.0; c.tick(10.0, audio); }
        c.tick(10.0, 50.0);                       // 過檔尾 → 位置繞回 ~0
        CHECK(c.timeMs() > 50000.0);              // 不能被拉回開頭
        for (int i = 0; i < 100; ++i) { audio = 100.0 + i * 10.0; c.tick(10.0, audio); }
        CHECK(c.timeMs() > 50000.0 + 900.0);      // 音訊從 0 爬升也不得回拉
    }

    // (c) 播放中的小幅回拉仍然生效（防矯枉過正）
    {
        SongClock c(0.0);
        c.seek(20000.0);
        double audio = 20010.0;
        c.tick(10.0, audio);                      // 對齊 time≈audio
        for (int i = 0; i < 30; ++i) { audio += 1.0; c.tick(10.0, audio); }  // 時鐘快 9ms/tick
        CHECK(c.timeMs() < 20100.0);              // 漂移 >100ms 時被小幅拉回
        CHECK(c.timeMs() > 20010.0);              // 整體仍持續前進
    }
}

void testScores() {
    std::printf("scores\n");
    const fs::path f = fs::temp_directory_path() / "overkey_test" / "scores.txt";
    fs::create_directories(f.parent_path());
    fs::remove(f);
    const std::string key = "maps/Song [HARD].osu";  // key 含空白/括號
    {
        ScoreBook sb(f);
        CHECK(!sb.best(key).valid);                                   // 一開始無紀錄
        CHECK(sb.submit(key, {1000, 90.0, "A", 100}));               // 新紀錄
        CHECK(!sb.submit(key, {800, 80.0, "B", 50}));                // 較低不更新
        CHECK(sb.submit(key, {1500, 95.5, "S", 200}));               // 更高刷新
        const ScoreRecord r = sb.best(key);
        CHECK(r.valid && r.score == 1500 && r.grade == "S" && r.maxCombo == 200);
        CHECK(r.accuracy > 95.4 && r.accuracy < 95.6);
    }
    {
        ScoreBook sb2(f);                                            // 重載：持久化
        const ScoreRecord r = sb2.best(key);
        CHECK(r.valid && r.score == 1500 && r.grade == "S");
    }
}

// 即時 pp / Quaver rating 計數器語意：每次以「從頭到此刻的譜面前綴」重算
// 難度配上目前的判定 acc；前綴為空時為 0，打完時（前綴=整張圖）與
// estimateRatings(整圖難度) ——即結算畫面數字——一致。
void testLiveRatings() {
    std::printf("live ratings\n");
    const std::vector<ManiaNote> notes = {{0, 1000, -1}, {1, 2000, -1}, {2, 3000, -1},
                                          {3, 4000, 4500}};  // 3 tap + 1 LN
    PlaySession s(notes);

    // 前綴為空（第一個音符還沒到）：兩個計數器都是 0，且不得出現 NaN/inf
    RatingEstimate live = estimateLiveRatings(s, 7, 500.0);
    CHECK(live.quaverRating == 0.0 && live.osuPP == 0.0);

    // 極短前綴（1~2 顆音符）：此處是 QSS continuity 正規化的退化路徑，
    // 必須回傳有限值（回歸：即時重算常態性遇到 <1s 前綴）
    s.press(0, 1000);
    live = estimateLiveRatings(s, 7, 1000.0);  // 前綴 1 顆
    CHECK(std::isfinite(live.quaverRating) && std::isfinite(live.osuPP));
    s.press(1, 2000);
    live = estimateLiveRatings(s, 7, 2100.0);  // 前綴 2 顆、acc 已累積
    CHECK(std::isfinite(live.quaverRating) && std::isfinite(live.osuPP));
    CHECK(live.quaverRating > 0.0);

    // 全數打完（LN 按住到尾 → 尾 Perfect）：前綴=整張圖 → 與結算數字一致
    s.press(2, 3000);
    s.press(3, 4000);
    s.advance(4500);
    live = estimateLiveRatings(s, 7, 1.0e9);
    const RatingEstimate full = estimateRatings(quaverDifficulty(notes, 7, 1.0f), s);
    CHECK(live.quaverRating == full.quaverRating);
    CHECK(live.osuPP == full.osuPP);
}

// 選單搜尋用的字串工具：不分大小寫子字串比對 + UTF-8 輸入編輯
void testStrutil() {
    std::printf("strutil\n");
    // 大小寫不敏感子字串（清單標籤格式 "Artist - Title [Diff]"）
    CHECK(strutil::icontains("xi - FREEDOM DiVE [4K]", "freedom"));
    CHECK(strutil::icontains("Camellia - Ghost [7K EX]", "ghost"));
    CHECK(strutil::icontains("Camellia - Ghost [7K EX]", "ex]"));
    CHECK(!strutil::icontains("abc", "abcd"));
    CHECK(strutil::icontains("any", ""));       // 空 query 不篩
    CHECK(!strutil::icontains("", "x"));
    // UTF-8 標籤：位元組子字串比對（東方 = \xE6\x9D\xB1\xE6\x96\xB9）
    CHECK(strutil::icontains("IOSYS - \xE6\x9D\xB1\xE6\x96\xB9 arrange", "\xE6\x9D\xB1\xE6\x96\xB9"));
    CHECK(!strutil::icontains("xi - Parousia", "\xE6\x9D\xB1\xE6\x96\xB9"));

    // appendUtf8 / popUtf8：ASCII、三位元組（中 U+4E2D）、四位元組（U+1F600）
    std::string s;
    strutil::appendUtf8(s, 'A');
    strutil::appendUtf8(s, 0x4E2D);
    strutil::appendUtf8(s, 0x1F600);
    CHECK(s == "A\xE4\xB8\xAD\xF0\x9F\x98\x80");
    strutil::popUtf8(s);
    CHECK(s == "A\xE4\xB8\xAD");
    strutil::popUtf8(s);
    CHECK(s == "A");
    strutil::popUtf8(s);
    CHECK(s.empty());
    strutil::popUtf8(s);  // 空字串安全
    CHECK(s.empty());
}

void testLibrary() {
    std::printf("library sort/group\n");

    std::string l1 = "alpha song", l2 = "Beta song", l3 = "gamma song";
    MapMetrics m1, m2, m3;

    // Title：ASCII 不區分大小寫；desc 反轉
    SortRow t1{&l1, &m1, nullptr}, t2{&l2, &m2, nullptr}, t3{&l3, &m3, nullptr};
    CHECK(sortRowLess(SortKey::Title, true, t1, t2));
    CHECK(sortRowLess(SortKey::Title, true, t2, t3));  // B < g（折疊大小寫）
    CHECK(!sortRowLess(SortKey::Title, true, t3, t2));
    CHECK(sortRowLess(SortKey::Title, false, t3, t1));  // desc 反轉
    CHECK(groupName(SortKey::Title, t1) == "A");
    CHECK(groupName(SortKey::Title, t2) == "B");
    std::string lCjk = "中文字";
    SortRow tCjk{&lCjk, &m1, nullptr};
    CHECK(groupName(SortKey::Title, tCjk) == "#");

    // Grade：等第佳者前、同等第 acc 高者前、無紀錄墊底（desc 仍墊底）
    ScoreRecord sSS{100, 99.5, "SS", 1, true};
    ScoreRecord sSa{90, 96.0, "S", 1, true};
    ScoreRecord sSb{80, 95.5, "S", 1, true};
    ScoreRecord sD{10, 60.0, "D", 1, true};
    ScoreRecord sNone;
    CHECK(gradeRank(&sSS) == 6);
    CHECK(gradeRank(&sD) == 1);
    CHECK(gradeRank(&sNone) == 0);
    CHECK(gradeRank(nullptr) == 0);
    SortRow gSS{&l1, &m1, &sSS}, gSa{&l2, &m2, &sSa}, gSb{&l3, &m3, &sSb};
    SortRow gD{&l2, &m2, &sD}, gNo{&l3, &m3, &sNone};
    CHECK(sortRowLess(SortKey::Grade, true, gSS, gSa));
    CHECK(sortRowLess(SortKey::Grade, true, gSa, gSb));  // 同 S、acc 高者在前
    CHECK(sortRowLess(SortKey::Grade, true, gD, gNo));   // 有紀錄在無紀錄前
    CHECK(sortRowLess(SortKey::Grade, false, gD, gNo));  // desc 反轉但無紀錄仍墊底
    CHECK(groupName(SortKey::Grade, gSS) == "SS");
    CHECK(groupName(SortKey::Grade, gNo) == "UNPLAYED");

    // Difficulty：星級低者前（asc）；未評級墊底（desc 也一樣）
    MapMetrics d1, d2, d0;
    d1.osuStar = 2.5;
    d1.quaverDiff = 5.0;
    d2.osuStar = 6.0;
    d2.quaverDiff = 15.0;
    SortRow r1{&l1, &d1, nullptr}, r2{&l2, &d2, nullptr}, r0{&l3, &d0, nullptr};
    CHECK(sortRowLess(SortKey::Difficulty, true, r1, r2));
    CHECK(sortRowLess(SortKey::Difficulty, false, r2, r1));
    CHECK(sortRowLess(SortKey::Difficulty, false, r2, r0));
    CHECK(!sortRowLess(SortKey::Difficulty, false, r0, r2));
    CHECK(groupName(SortKey::Difficulty, r1) == "2 - 4 ★");
    CHECK(groupName(SortKey::Difficulty, r2) == "6 - 8 ★");
    CHECK(groupName(SortKey::Difficulty, r0) == "UNRATED");

    // Length / Notes：需 BeatmapInfo；缺資料墊底
    MapMetrics i1, i2;
    BeatmapInfo bi1, bi2;
    bi1.lengthMs = 45'000;
    bi1.noteCount = 300;
    bi2.lengthMs = 200'000;
    bi2.noteCount = 2500;
    i1.info = bi1;
    i2.info = bi2;
    SortRow len1{&l1, &i1, nullptr}, len2{&l2, &i2, nullptr}, len0{&l3, &m3, nullptr};
    CHECK(sortRowLess(SortKey::Length, true, len1, len2));
    CHECK(sortRowLess(SortKey::Length, true, len2, len0));  // 缺 info 墊底
    CHECK(groupName(SortKey::Length, len1) == "0:00 - 0:59");
    CHECK(groupName(SortKey::Length, len2) == "3:00 - 4:59");
    CHECK(sortRowLess(SortKey::Notes, true, len1, len2));
    CHECK(groupName(SortKey::Notes, len1) == "0 - 499");
    CHECK(groupName(SortKey::Notes, len2) == "2000 - 3999");
    CHECK(groupName(SortKey::Notes, len0) == "UNKNOWN");

    // loadMetrics 實際載入：info-only 不算難度；withDiff 補上（且資訊不會重載遺失）
    std::string ho;
    ho += std::to_string(colX(0, 7)) + ",192,1000,1,0,0:0:0:0:\n";
    ho += std::to_string(colX(3, 7)) + ",192,1400,128,0,1800:0:0:0:0:\n";
    const fs::path p = writeOsu("lib.osu", 3, 7, -1, ho);
    MapMetrics m;
    loadMetrics(p, m, /*withDiff=*/false);
    CHECK(m.info.has_value());
    CHECK(!m.quaverDiff.has_value());
    CHECK(m.info->noteCount == 2);
    loadMetrics(p, m, /*withDiff=*/true);
    CHECK(m.info.has_value());
    CHECK(m.quaverDiff.has_value());
    CHECK(m.osuStar.has_value());
    CHECK(sortNeedsInfo(SortKey::Length));
    CHECK(sortNeedsInfo(SortKey::Notes));
    CHECK(!sortNeedsInfo(SortKey::Grade));
    CHECK(sortNeedsDiff(SortKey::Difficulty));
    CHECK(!sortNeedsDiff(SortKey::Title));

    // 介面完整性
    CHECK(std::string(sortKeyName(SortKey::Title)) == "TITLE");
    SortKey k = SortKey::Title;
    for (int i = 0; i < 5; ++i) k = nextSortKey(k);
    CHECK(k == SortKey::Title);
}

// 鍵數篩選器（4K–7K bitmask）：範圍檢查、切換、最後一種不可關
void testKeyFilter() {
    std::printf("key filter\n");
    CHECK(keyFilterAllows(kKeyFilterAll, 4));
    CHECK(keyFilterAllows(kKeyFilterAll, 5));
    CHECK(keyFilterAllows(kKeyFilterAll, 6));
    CHECK(keyFilterAllows(kKeyFilterAll, 7));
    CHECK(!keyFilterAllows(kKeyFilterAll, 3));   // 支援範圍外
    CHECK(!keyFilterAllows(kKeyFilterAll, 8));

    int m = toggleKeyFilter(kKeyFilterAll, 5);   // 關 5K
    CHECK(!keyFilterAllows(m, 5));
    CHECK(keyFilterAllows(m, 4) && keyFilterAllows(m, 6) && keyFilterAllows(m, 7));
    m = toggleKeyFilter(m, 5);                   // 再開 5K
    CHECK(keyFilterAllows(m, 5));

    m = toggleKeyFilter(toggleKeyFilter(toggleKeyFilter(kKeyFilterAll, 4), 5), 6);
    CHECK(m == (1 << 7));                        // 只剩 7K
    CHECK(toggleKeyFilter(m, 7) == m);           // 唯一亮著的關不掉（避免篩空）
    CHECK(toggleKeyFilter(kKeyFilterAll, 2) == kKeyFilterAll);  // 範圍外忽略

    // 各鍵數預設鍵位表長度正確（支援範圍外回空）
    Settings s;
    CHECK(s.laneKeys(4).size() == 4 && s.laneKeys(4)[0] == s.keys4[0]);
    CHECK(s.laneKeys(5).size() == 5 && s.laneKeys(5)[2] == s.keys5[2]);
    CHECK(s.laneKeys(6).size() == 6 && s.laneKeys(6)[0] == s.keys6[0]);
    CHECK(s.laneKeys(7).size() == 7 && s.laneKeys(7)[3] == s.keys[3]);
    CHECK(s.laneKeys(3).empty() && s.laneKeys(8).empty());
}

}  // namespace

int main() {
    testMapParsing();
    testQuaParsing();
    testJudgmentTiers();
    testHitEventError();
    testScoringAndMiss();
    testLongNote();
    testSongClock();
    testSongClockPastAudioEnd();
    testLiveRatings();
    testScores();
    testStrutil();
    testLibrary();
    testKeyFilter();

    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "overkey_test", ec);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
