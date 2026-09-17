#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <raylib.h>
#include "library.h"
#include "map.h"

#include "raii.h"
#include "render.h"
#include "song_select.h"
#include "strutil.h"

namespace {
constexpr double kPreviewDebounce = 0.25;  // hover 後延遲多久才載入試聽，避免快速捲動狂載
constexpr int kRowH = 46;
constexpr int kHeaderH = 34;               // 群組標頭列高
constexpr int kListTop = 170;
constexpr int kListBottom = 80;  // 底部保留給提示
constexpr int kListW = 520;      // 左側清單寬度，右側為詳情面板
constexpr int kPanelX = kListW + 40;

// 文字超過 maxW 像素時尾端以省略號截斷
std::string ellipsize(const std::string& s, int fontSize, int maxW) {
    if (MeasureText(s.c_str(), fontSize) <= maxW) return s;
    std::string t = s;
    while (!t.empty() && MeasureText((t + "...").c_str(), fontSize) > maxW) t.pop_back();
    return t + "...";
}
}  // namespace

SongSelect::SongSelect(std::filesystem::path mapsDir)
    : mapsDir_(std::move(mapsDir)), importer_(mapsDir_) {
    std::error_code ec;
    if (std::filesystem::is_directory(mapsDir_, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(
                 mapsDir_, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            Entry e;
            if (makeEntry(it->path(), e)) entries_.push_back(std::move(e));
        }
    }
    rebuildView({});  // 空 query → 全清單
    importer_.start();  // 開背景執行緒掃描/解壓 .osz/.qp，執行期間增量餵進清單
}

SongSelect::~SongSelect() { stopMetricLoad(); }

bool SongSelect::makeEntry(const std::filesystem::path& p, Entry& out) const {
    if (p.extension() != ".osu" && p.extension() != ".qua") return false;
    const BeatmapHeader h = probeBeatmap(p);
    if (!h.isSupported()) return false;
    // 優先用譜面標題，缺 metadata 時退回相對路徑
    std::string label = h.label();
    if (label.empty()) label = p.lexically_relative(mapsDir_).replace_extension("").string();
    out = Entry{p, std::move(label), {}};
    return true;
}

void SongSelect::ingestNewMaps() {
    std::vector<std::filesystem::path> fresh = importer_.drainNewMaps();
    if (fresh.empty()) return;

    // 重排會打亂索引，先記住目前選取項的路徑，插完再依路徑還原選取。
    const std::filesystem::path selPath = selectedPath();

    for (const auto& p : fresh) {
        if (std::any_of(entries_.begin(), entries_.end(),
                        [&](const Entry& e) { return e.path == p; }))
            continue;  // 與初次掃描重複則略過
        Entry e;
        if (makeEntry(p, e)) entries_.push_back(std::move(e));
    }
    metricTotal_ = 0;  // 新譜面缺指標 → 允許背景計算開新一輪
    rebuildView(selPath);  // 依現有排序 + query 重建並還原選取
}

void SongSelect::rebuildView(const std::filesystem::path& preserve) {
    // 等第排序／分組需要成績；預先整批查出（查表 log n，只在 Grade 鍵時做）
    std::vector<ScoreRecord> bests;
    if (sortKey_ == SortKey::Grade && scores_) {
        bests.resize(entries_.size());
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
            bests[i] = scores_->best(entries_[i].path.string());
    }
    auto rowOf = [&](int i) {
        return SortRow{&entries_[i].label, &entries_[i].metrics,
                       bests.empty() ? nullptr : &bests[i]};
    };

    // 1) 依排序鍵排全部譜面（索引排序；entries_ 本身順序不動）
    std::vector<int> order(entries_.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return sortRowLess(sortKey_, sortAsc_, rowOf(a), rowOf(b));
    });

    // 2) 套用 query 篩選並插入群組標頭（搜尋中不插標頭，維持平清單）
    rows_.clear();
    itemCount_ = 0;
    std::string lastGroup;
    for (const int idx : order) {
        if (!query_.empty() && !strutil::icontains(entries_[idx].label, query_)) continue;
        if (query_.empty()) {
            const std::string g = groupName(sortKey_, rowOf(idx));
            if (g != lastGroup) {
                rows_.push_back(Row{-1, g, 0});
                lastGroup = g;
            }
        }
        rows_.push_back(Row{idx, {}, itemCount_++});
    }

    // 3) 還原選取（優先 preserve 路徑，否則第一個項目列）
    selected_ = -1;
    if (!preserve.empty()) {
        for (int v = 0; v < static_cast<int>(rows_.size()); ++v) {
            if (!rows_[v].isHeader() && entries_[rows_[v].entry].path == preserve) {
                selected_ = v;
                break;
            }
        }
    }
    if (selected_ < 0) {
        for (int v = 0; v < static_cast<int>(rows_.size()); ++v) {
            if (!rows_[v].isHeader()) {
                selected_ = v;
                break;
            }
        }
    }
    if (selected_ < 0) selected_ = 0;  // 完全無項目（搜尋無結果）
    scroll_ = 0;
    clampScroll();
}

std::filesystem::path SongSelect::selectedPath() const {
    if (rows_.empty()) return {};
    const Row& r = rows_[std::clamp(selected_, 0, static_cast<int>(rows_.size()) - 1)];
    return r.isHeader() ? std::filesystem::path{} : entries_[r.entry].path;
}

void SongSelect::stepSelection(int dir) {
    if (rows_.empty()) return;
    const int n = static_cast<int>(rows_.size());
    for (int step = 0; step < n; ++step) {
        selected_ = (selected_ + dir + n) % n;
        if (!rows_[selected_].isHeader()) return;  // 跳到項目列為止
    }
}

void SongSelect::clampScroll() {
    if (rows_.empty()) return;
    selected_ = std::clamp(selected_, 0, static_cast<int>(rows_.size()) - 1);
    if (rows_[selected_].isHeader()) stepSelection(1);  // 保證停在項目列
    const int visible = (kVirtualH - kListTop - kListBottom) / kRowH;
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
    scroll_ = std::max(0, scroll_);
}

void SongSelect::startMetricLoad() {
    // 收集缺指標的譜面（難度排序也要一併載入摘要）
    const bool needDiff = sortNeedsDiff(sortKey_);
    const bool needInfo = sortNeedsInfo(sortKey_) || needDiff;
    std::vector<std::pair<std::filesystem::path, bool>> todo;
    if (needInfo) {
        for (const Entry& e : entries_) {
            const bool d = needDiff && !e.metrics.quaverDiff;
            const bool i = d || !e.metrics.info;
            if (i) todo.emplace_back(e.path, d);
        }
    }
    if (todo.empty()) return;  // 全數快取齊全 → 不用開工

    stopMetricLoad();
    metricTotal_ = static_cast<int>(todo.size());
    metricProgress_ = 0;
    metricCancel_ = false;
    metricRunning_ = true;
    metricWorker_ = std::thread([this, todo = std::move(todo)] {
        for (const auto& [path, withDiff] : todo) {
            if (metricCancel_) break;
            MapMetrics m;
            loadMetrics(path, m, withDiff);
            {
                std::lock_guard<std::mutex> lk(metricM_);
                metricDone_.emplace_back(path, std::move(m));
            }
            ++metricProgress_;
        }
        metricRunning_ = false;
    });
}

void SongSelect::stopMetricLoad() {
    if (metricWorker_.joinable()) {
        metricCancel_ = true;
        metricWorker_.join();
    }
    metricRunning_ = false;
    drainMetricResults();
    metricTotal_ = 0;
    metricProgress_ = 0;
}

bool SongSelect::drainMetricResults() {
    std::deque<std::pair<std::filesystem::path, MapMetrics>> done;
    {
        std::lock_guard<std::mutex> lk(metricM_);
        done.swap(metricDone_);
    }
    if (done.empty()) return false;
    for (auto& [path, m] : done) {
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [&](const Entry& e) { return e.path == path; });
        if (it == entries_.end()) continue;
        if (!it->metrics.info && m.info) it->metrics.info = m.info;
        if (!it->metrics.quaverDiff && m.quaverDiff) {
            it->metrics.quaverDiff = m.quaverDiff;
            it->metrics.osuStar = m.osuStar;
        }
    }
    return true;
}

MenuResult SongSelect::run(Viewport& vp, float musicVolume, const ScoreBook& scores,
                           bool& autoPlay, float& rate) {
    SetWindowTitle("OverKey - Select Song");
    scores_ = &scores;

    // 每幀同步 mod 旗標到成員，供 draw() 顯示指示器
    auto syncMods = [&] { autoPlay_ = autoPlay; rate_ = rate; };
    syncMods();

    std::optional<MusicRes> preview;        // 目前試聽
    std::filesystem::path previewPath;      // 正在播放的音訊檔（空＝無）
    double selChangedAt = GetTime();        // 上次切換選取的時間（防抖）
    double previewStart = 0.0;              // 試聽循環的起點（秒）
    std::optional<TextureRes> cover;        // 目前封面圖
    std::filesystem::path coverPath;        // 已載入的封面檔（空＝無）
    int lastSel = selected_;

    // 每次回選單重置搜尋狀態，並重建（成績可能已更新 → 反映到等第排序/分組）
    query_.clear();
    searching_ = false;
    rebuildView({});

    while (!WindowShouldClose()) {
        ingestNewMaps();  // 背景解壓的新譜面即時進清單
        syncMods();
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

        // 背景指標計算：序列到貨就併入並重排；全齊後視需要開新一輪
        if (drainMetricResults()) rebuildView(selectedPath());
        if ((sortNeedsInfo(sortKey_) || sortNeedsDiff(sortKey_)) && !metricRunning_ &&
            metricTotal_ == 0)
            startMetricLoad();

        // 文字輸入：搜尋模式吃進 query，否則丟棄（避免佇列積壓進入搜尋時噴出）
        for (int ch = GetCharPressed(); ch > 0; ch = GetCharPressed()) {
            if (!searching_ || ch < 32) continue;
            const std::filesystem::path keep = selectedPath();
            strutil::appendUtf8(query_, static_cast<uint32_t>(ch));
            rebuildView(keep);
        }
        if (searching_ && IsKeyPressed(KEY_BACKSPACE) && !query_.empty()) {
            const std::filesystem::path keep = selectedPath();
            strutil::popUtf8(query_);
            rebuildView(keep);
        }
        if (!searching_ && (IsKeyPressed(KEY_SLASH) || IsKeyPressed(KEY_KP_DIVIDE)))
            searching_ = true;

        if (!rows_.empty()) {
            int dir = 0;
            if (IsKeyPressed(KEY_DOWN)) ++dir;
            if (IsKeyPressed(KEY_UP)) --dir;
            const float wheel = GetMouseWheelMove();
            if (wheel > 0) --dir;
            if (wheel < 0) ++dir;
            if (dir) {
                stepSelection(dir > 0 ? 1 : -1);
                clampScroll();
            }
            ensureSelectedInfo();
            if (selected_ != lastSel) {  // 換選取：重啟防抖（先不停試聽）
                lastSel = selected_;
                selChangedAt = GetTime();
            }

            if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) &&
                !rows_[selected_].isHeader()) {
                return {MenuAction::Play, entries_[rows_[selected_].entry].path};
            }
        }
        if (IsKeyPressed(KEY_TAB)) return {MenuAction::Settings, {}};
        if (!searching_) {  // 搜尋輸入時字母/方向鍵讓給打字與清單導覽
            if (IsKeyPressed(KEY_M)) autoPlay = !autoPlay;  // 切換 auto-play
            if (IsKeyPressed(KEY_LEFT)) rate = std::max(0.5f, std::round((rate - 0.05f) / 0.05f) * 0.05f);
            if (IsKeyPressed(KEY_RIGHT)) rate = std::min(2.0f, std::round((rate + 0.05f) / 0.05f) * 0.05f);
            if (IsKeyPressed(KEY_S)) {  // S 換排序鍵；Shift+S 反轉方向
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    sortAsc_ = !sortAsc_;
                } else {
                    sortKey_ = nextSortKey(sortKey_);
                    stopMetricLoad();  // 換鍵 → 以新需求重開背景計算
                }
                rebuildView(selectedPath());
            }
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (searching_) {  // 先清搜尋，再按一次才離開
                const std::filesystem::path keep = selectedPath();
                searching_ = false;
                if (!query_.empty()) {
                    query_.clear();
                    rebuildView(keep);
                }
            } else {
                return {MenuAction::Quit, {}};
            }
        }

        // 副歌試聽：停穩超過防抖時間後，只有「音訊檔不同」才重載
        //（同一首歌切不同難度→音訊檔相同→續播不重啟）
        if (!rows_.empty() && !rows_[selected_].isHeader() &&
            GetTime() - selChangedAt > kPreviewDebounce) {
            const Entry& e = entries_[rows_[selected_].entry];
            std::filesystem::path desired;
            if (e.metrics.info && !e.metrics.info->audioFilename.empty())
                desired = e.path.parent_path() / e.metrics.info->audioFilename;
            if (desired != previewPath) {
                preview.reset();
                previewPath = desired;
                if (!desired.empty()) {
                    preview.emplace(desired.string().c_str());
                    if (preview->valid()) {
                        SetMusicVolume(preview->get(), musicVolume);
                        PlayMusicStream(preview->get());
                        previewStart = e.metrics.info->previewTimeMs >= 0
                                           ? e.metrics.info->previewTimeMs / 1000.0
                                           : GetMusicTimeLength(preview->get()) * 0.4;
                        SeekMusicStream(preview->get(), static_cast<float>(previewStart));
                    } else {
                        previewPath.clear();
                    }
                }
            }
        }
        if (preview && preview->valid()) {
            UpdateMusicStream(preview->get());
            // 播放至結尾 → 回到副歌起點循環
            if (GetMusicTimePlayed(preview->get()) >=
                GetMusicTimeLength(preview->get()) - 0.1f) {
                SeekMusicStream(preview->get(), static_cast<float>(previewStart));
            }
        }

        // 封面圖：停穩後只有「檔案不同」才重載
        if (!rows_.empty() && !rows_[selected_].isHeader() &&
            GetTime() - selChangedAt > kPreviewDebounce) {
            const Entry& e = entries_[rows_[selected_].entry];
            std::filesystem::path desired;
            if (e.metrics.info && !e.metrics.info->backgroundFilename.empty())
                desired = e.path.parent_path() / e.metrics.info->backgroundFilename;
            if (desired != coverPath) {
                cover.reset();
                coverPath = desired;
                std::error_code ec;
                if (!desired.empty() && std::filesystem::exists(desired, ec)) {
                    cover.emplace(desired.string().c_str());
                    if (!cover->valid()) cover.reset();
                }
                if (!cover) coverPath.clear();
            }
        }

        vp.begin(Color{18, 18, 24, 255});
        if (cover && cover->valid()) {  // 詳情面板背景（cover-fit 裁切 + 變暗）
            const Texture2D& tex = cover->get();
            const Rectangle dst{static_cast<float>(kListW + 20),
                                static_cast<float>(kListTop - 10),
                                static_cast<float>(kVirtualW - (kListW + 20) - 10),
                                static_cast<float>(kVirtualH - kListBottom - (kListTop - 10))};
            const float sw = static_cast<float>(tex.width), sh = static_cast<float>(tex.height);
            const float sc = std::max(dst.width / sw, dst.height / sh);
            const float cw = dst.width / sc, ch = dst.height / sc;
            const Rectangle src{(sw - cw) / 2, (sh - ch) / 2, cw, ch};
            DrawTexturePro(tex, src, dst, {0, 0}, 0, WHITE);
            DrawRectangleRec(dst, Fade(BLACK, 0.6f));
        }
        draw();
        vp.end();
    }
    return {MenuAction::Quit, {}};  // 視窗關閉
}

void SongSelect::ensureSelectedInfo() {
    if (rows_.empty()) return;
    const Row& r = rows_[selected_];
    if (r.isHeader()) return;
    Entry& e = entries_[r.entry];
    if (!e.metrics.info || !e.metrics.quaverDiff) loadMetrics(e.path, e.metrics, true);
}

void SongSelect::draw() const {
    const int w = kVirtualW;

    DrawText("OVERKEY", 40, 50, 56, RAYWHITE);
    DrawText("SELECT SONG", 42, 115, 24, GRAY);

    {  // mod 指示器（右上角）
        int mx = w - 40;
        if (autoPlay_) {
            const char* a = "AUTO";
            mx -= MeasureText(a, 22);
            DrawText(a, mx, 60, 22, GOLD);
            mx -= 24;
        }
        if (rate_ != 1.0f) {
            const char* r = TextFormat("%.2fx", rate_);
            mx -= MeasureText(r, 22);
            DrawText(r, mx, 60, 22, GOLD);
            mx -= 24;
        }
    }

    if (entries_.empty()) {
        const char* msg = "找不到 mania 4K/7K 譜面（請確認 maps 目錄路徑）";
        DrawText(msg, w / 2 - MeasureText(msg, 28) / 2, kVirtualH / 2 - 14, 28, RED);
        const char* hint = "Press ESC to exit";
        DrawText(hint, w / 2 - MeasureText(hint, 22) / 2, kVirtualH - 60, 22,
                 Fade(RAYWHITE, 0.6f));
        return;
    }

    // 左側清單（標頭列 + 項目列混排；項目列高才佔滿 kRowH）
    const int visible = (kVirtualH - kListTop - kListBottom) / kRowH;
    const int end = std::min(static_cast<int>(rows_.size()), scroll_ + visible);
    for (int i = scroll_; i < end; ++i) {
        const Row& row = rows_[i];
        const int y = kListTop + (i - scroll_) * kRowH;
        if (row.isHeader()) {  // 群組標頭：細字 + 分隔線
            DrawText(row.header.c_str(), 44, y + (kRowH - kHeaderH) / 2, 18, GOLD);
            const int tx = 44 + MeasureText(row.header.c_str(), 18) + 12;
            DrawLine(tx, y + kRowH / 2 - 1, kListW - 20, y + kRowH / 2 - 1,
                     Color{70, 55, 80, 255});
            continue;
        }
        const bool sel = (i == selected_);
        if (sel) {
            DrawRectangle(20, y - 4, kListW - 20, kRowH - 6, Color{70, 55, 80, 255});
            DrawRectangle(20, y - 4, 6, kRowH - 6, GOLD);
        }
        const std::string text = ellipsize(entries_[row.entry].label, 24, kListW - 60);
        DrawText(text.c_str(), 44, y, 24, sel ? RAYWHITE : Fade(RAYWHITE, 0.7f));
    }

    // 計數器 + 排序狀態列
    if (itemCount_ > 0 && !rows_.empty() && !rows_[selected_].isHeader())
        DrawText(TextFormat("%d / %d", rows_[selected_].itemPos + 1, itemCount_), 40, 145,
                 20, GRAY);
    {
        const std::string s =
            std::string(sortKeyName(sortKey_)) + (sortAsc_ ? " ASC" : " DESC");
        DrawText(s.c_str(), 190, 145, 20, GOLD);
    }
    if (searching_ || !query_.empty()) {  // 搜尋狀態列（計數器右側）
        const std::string s = "Search: " + query_ + (searching_ ? "_" : "");
        DrawText(s.c_str(), 420, 145, 20, searching_ ? GOLD : GRAY);
    }

    // 右側詳情面板
    DrawLine(kListW + 20, kListTop - 10, kListW + 20, kVirtualH - kListBottom,
             Color{40, 40, 50, 255});
    if (rows_.empty()) {  // 有譜面但無符合結果
        DrawText("沒有符合的譜面", 44, kListTop, 24, Fade(RAYWHITE, 0.6f));
    }
    // 選取維持在項目列上；rows_ 空時 entries_ 必非空（draw 前段已攔截空清單）
    const Entry& e =
        rows_.empty() ? entries_[0] : entries_[std::max(0, rows_[selected_].entry)];
    if (!rows_.empty() && !rows_[selected_].isHeader() && e.metrics.info) {
        const BeatmapInfo& bi = *e.metrics.info;
        int y = kListTop;
        const int panelW = kVirtualW - kPanelX - 30;
        auto field = [&](const char* label, const std::string& value, int size, Color c) {
            if (value.empty()) return;
            DrawText(label, kPanelX, y, 18, GRAY);
            y += 22;
            DrawText(ellipsize(value, size, panelW).c_str(), kPanelX, y, size, c);
            y += size + 14;
        };
        field("TITLE", bi.title.empty() ? e.label : bi.title, 26, RAYWHITE);
        field("ARTIST", bi.artist, 22, Fade(RAYWHITE, 0.85f));
        field("DIFFICULTY", bi.version, 22, GOLD);
        if (e.metrics.quaverDiff && e.metrics.osuStar) {
            DrawText(TextFormat("QUAVER DIFF %.2f", *e.metrics.quaverDiff), kPanelX, y, 22,
                     RAYWHITE);
            y += 30;
            DrawText(TextFormat("OSU STAR %.2f★", *e.metrics.osuStar), kPanelX, y, 22,
                     RAYWHITE);
            y += 30;
        }

        y += 8;
        const int sec = bi.lengthMs / 1000;
        const bool supported = (bi.keyCount == 4 || bi.keyCount == 7);
        DrawText(TextFormat("Keys     %dK", bi.keyCount), kPanelX, y, 22,
                 supported ? GREEN : ORANGE);
        y += 30;
        DrawText(TextFormat("Notes    %d", bi.noteCount), kPanelX, y, 22, RAYWHITE);
        y += 30;
        DrawText(TextFormat("Length   %d:%02d", sec / 60, sec % 60), kPanelX, y, 22, RAYWHITE);
        y += 44;

        // 最佳成績
        const ScoreRecord rec = scores_ ? scores_->best(e.path.string()) : ScoreRecord{};
        DrawText("BEST", kPanelX, y, 18, GRAY);
        y += 24;
        if (rec.valid) {
            DrawText(TextFormat("%s   %.2f%%", rec.grade.c_str(), rec.accuracy), kPanelX, y, 24,
                     GOLD);
            y += 28;
            DrawText(TextFormat("%d   x%d", rec.score, rec.maxCombo), kPanelX, y, 20,
                     Fade(RAYWHITE, 0.8f));
        } else {
            DrawText("not played", kPanelX, y, 20, Fade(RAYWHITE, 0.5f));
        }
    }

    // 背景指標計算進度（左下角）
    if (metricRunning_ && metricTotal_ > 0)
        DrawText(TextFormat("analyzing %d/%d", metricProgress_.load(), metricTotal_), 40,
                 kVirtualH - 78, 18, GOLD);

    const char* hint =
        searching_ ? "type to search   BACKSPACE delete   ENTER play   ESC clear"
                   : "UP/DOWN select   ENTER play   / search   S sort (Shift+S order)   M auto   "
                     "LEFT/RIGHT rate   TAB settings   F11 fullscreen   ESC quit";
    DrawText(hint, w / 2 - MeasureText(hint, 22) / 2, kVirtualH - 55, 22,
             Fade(RAYWHITE, 0.6f));
}
