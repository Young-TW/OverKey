#include <algorithm>
#include <optional>
#include <system_error>
#include <utility>

#include <raylib.h>

#include "raii.h"
#include "render.h"
#include "song_select.h"

namespace {
constexpr double kPreviewDebounce = 0.25;  // hover 後延遲多久才載入試聽，避免快速捲動狂載
constexpr int kRowH = 46;
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

SongSelect::SongSelect(std::filesystem::path mapsDir) : mapsDir_(std::move(mapsDir)) {
    std::error_code ec;
    if (std::filesystem::is_directory(mapsDir_, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(
                 mapsDir_, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            const auto& p = it->path();
            if ((p.extension() == ".osu" || p.extension() == ".qua") &&
                probeBeatmap(p).isSupported()) {
                std::filesystem::path rel = p.lexically_relative(mapsDir_);
                std::string label = rel.replace_extension("").string();
                entries_.push_back({p, std::move(label)});
            }
        }
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.label < b.label; });
}

void SongSelect::clampScroll() {
    if (entries_.empty()) return;
    selected_ = std::clamp(selected_, 0, static_cast<int>(entries_.size()) - 1);
    const int visible = (kVirtualH - kListTop - kListBottom) / kRowH;
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
    scroll_ = std::max(0, scroll_);
}

MenuResult SongSelect::run(Viewport& vp, float musicVolume, const ScoreBook& scores) {
    SetWindowTitle("OverKey - Select Song");
    scores_ = &scores;

    std::optional<MusicRes> preview;        // 目前試聽
    std::filesystem::path previewPath;      // 正在播放的音訊檔（空＝無）
    double selChangedAt = GetTime();        // 上次切換選取的時間（防抖）
    double previewStart = 0.0;              // 試聽循環的起點（秒）
    int lastSel = selected_;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if (!entries_.empty()) {
            if (IsKeyPressed(KEY_DOWN)) ++selected_;
            if (IsKeyPressed(KEY_UP)) --selected_;
            selected_ -= GetMouseWheelMove() > 0 ? 1 : (GetMouseWheelMove() < 0 ? -1 : 0);
            selected_ = (selected_ + static_cast<int>(entries_.size())) %
                        static_cast<int>(entries_.size());
            clampScroll();
            ensureSelectedInfo();
            if (selected_ != lastSel) {  // 換選取：重啟防抖（先不停試聽）
                lastSel = selected_;
                selChangedAt = GetTime();
            }

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                return {MenuAction::Play, entries_[selected_].path};
            }
        }
        if (IsKeyPressed(KEY_TAB)) return {MenuAction::Settings, {}};
        if (IsKeyPressed(KEY_ESCAPE)) return {MenuAction::Quit, {}};

        // 副歌試聽：停穩超過防抖時間後，只有「音訊檔不同」才重載
        //（同一首歌切不同難度→音訊檔相同→續播不重啟）
        if (!entries_.empty() && GetTime() - selChangedAt > kPreviewDebounce) {
            const Entry& e = entries_[selected_];
            std::filesystem::path desired;
            if (e.info && !e.info->audioFilename.empty())
                desired = e.path.parent_path() / e.info->audioFilename;
            if (desired != previewPath) {
                preview.reset();
                previewPath = desired;
                if (!desired.empty()) {
                    preview.emplace(desired.string().c_str());
                    if (preview->valid()) {
                        SetMusicVolume(preview->get(), musicVolume);
                        PlayMusicStream(preview->get());
                        previewStart = e.info->previewTimeMs >= 0
                                           ? e.info->previewTimeMs / 1000.0
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

        vp.begin(Color{18, 18, 24, 255});
        draw();
        vp.end();
    }
    return {MenuAction::Quit, {}};  // 視窗關閉
}

void SongSelect::ensureSelectedInfo() {
    if (entries_.empty()) return;
    Entry& e = entries_[selected_];
    if (!e.info) e.info = loadBeatmapInfo(e.path);
}

void SongSelect::draw() const {
    const int w = kVirtualW;

    DrawText("OVERKEY", 40, 50, 56, RAYWHITE);
    DrawText("SELECT SONG", 42, 115, 24, GRAY);

    if (entries_.empty()) {
        const char* msg = "找不到 mania 4K/7K 譜面（請確認 maps 目錄路徑）";
        DrawText(msg, w / 2 - MeasureText(msg, 28) / 2, kVirtualH / 2 - 14, 28, RED);
        const char* hint = "Press ESC to exit";
        DrawText(hint, w / 2 - MeasureText(hint, 22) / 2, kVirtualH - 60, 22,
                 Fade(RAYWHITE, 0.6f));
        return;
    }

    // 左側清單
    const int visible = (kVirtualH - kListTop - kListBottom) / kRowH;
    const int end = std::min(static_cast<int>(entries_.size()), scroll_ + visible);
    for (int i = scroll_; i < end; ++i) {
        const int y = kListTop + (i - scroll_) * kRowH;
        const bool sel = (i == selected_);
        if (sel) {
            DrawRectangle(20, y - 4, kListW - 20, kRowH - 6, Color{70, 55, 80, 255});
            DrawRectangle(20, y - 4, 6, kRowH - 6, GOLD);
        }
        const std::string text = ellipsize(entries_[i].label, 24, kListW - 60);
        DrawText(text.c_str(), 44, y, 24, sel ? RAYWHITE : Fade(RAYWHITE, 0.7f));
    }

    DrawText(TextFormat("%d / %d", selected_ + 1, static_cast<int>(entries_.size())),
             40, 145, 20, GRAY);

    // 右側詳情面板
    DrawLine(kListW + 20, kListTop - 10, kListW + 20, kVirtualH - kListBottom,
             Color{40, 40, 50, 255});
    const Entry& e = entries_[selected_];
    if (e.info) {
        const BeatmapInfo& bi = *e.info;
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

    const char* hint =
        "UP/DOWN select   ENTER play   TAB settings   F11 fullscreen   ESC quit";
    DrawText(hint, w / 2 - MeasureText(hint, 22) / 2, kVirtualH - 55, 22,
             Fade(RAYWHITE, 0.6f));
}
