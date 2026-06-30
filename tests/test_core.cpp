// 前端無關 core 的單元測試：map 解析、PlaySession 判定/計分、SongClock。
// 不依賴 raylib 視窗/音訊，可 headless 執行。極簡 assert 框架。
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "map.h"
#include "play.h"
#include "scores.h"

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

    // 篩選：4K 支援、std/其他鍵數不支援
    CHECK(probeBeatmap(writeOsu("4k2.osu", 3, 4, -1, ho4)).isSupported());
    CHECK(!probeBeatmap(writeOsu("std.osu", 0, 5, -1, ho4)).isSupported());   // 非 mania
    CHECK(!probeBeatmap(writeOsu("5k.osu", 3, 5, -1, ho4)).isSupported());    // 5K 未支援

    // findHitSound：無取樣→空；有則找到並依優先序
    const fs::path dir = p.parent_path();
    CHECK(findHitSound(dir).empty());
    { std::ofstream(dir / "drum-hitnormal.ogg") << "x"; }
    CHECK(findHitSound(dir) == dir / "drum-hitnormal.ogg");
    { std::ofstream(dir / "normal-hitnormal.wav") << "x"; }
    CHECK(findHitSound(dir) == dir / "normal-hitnormal.wav");  // normal 優先於 drum
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

}  // namespace

int main() {
    testMapParsing();
    testJudgmentTiers();
    testScoringAndMiss();
    testLongNote();
    testSongClock();
    testScores();

    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "overkey_test", ec);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
