#include <algorithm>
#include <system_error>
#include <utility>

#include <raylib.h>

#include "song_select.h"

namespace {
constexpr int kRowH = 46;
constexpr int kListTop = 170;
constexpr int kListBottom = 80;  // 底部保留給提示
}  // namespace

SongSelect::SongSelect(std::filesystem::path mapsDir) : mapsDir_(std::move(mapsDir)) {
    std::error_code ec;
    if (std::filesystem::is_directory(mapsDir_, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(
                 mapsDir_, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            const auto& p = it->path();
            if (p.extension() == ".osu") {
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
    const int visible = (GetScreenHeight() - kListTop - kListBottom) / kRowH;
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
    scroll_ = std::max(0, scroll_);
}

std::optional<std::filesystem::path> SongSelect::run() {
    SetWindowTitle("OverKey - Select Song");

    while (!WindowShouldClose()) {
        if (!entries_.empty()) {
            if (IsKeyPressed(KEY_DOWN)) ++selected_;
            if (IsKeyPressed(KEY_UP)) --selected_;
            selected_ -= GetMouseWheelMove() > 0 ? 1 : (GetMouseWheelMove() < 0 ? -1 : 0);
            selected_ = (selected_ + static_cast<int>(entries_.size())) %
                        static_cast<int>(entries_.size());
            clampScroll();

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                return entries_[selected_].path;
            }
        }
        if (IsKeyPressed(KEY_ESCAPE)) return std::nullopt;

        BeginDrawing();
        ClearBackground(Color{18, 18, 24, 255});
        draw();
        EndDrawing();
    }
    return std::nullopt;  // 視窗關閉
}

void SongSelect::draw() const {
    const int w = GetScreenWidth();

    DrawText("OVERKEY", 40, 50, 56, RAYWHITE);
    DrawText("SELECT SONG", 42, 115, 24, GRAY);

    if (entries_.empty()) {
        const char* msg = "maps/ 內找不到 .osu 譜面";
        DrawText(msg, w / 2 - MeasureText(msg, 28) / 2, GetScreenHeight() / 2 - 14, 28, RED);
        const char* hint = "Press ESC to exit";
        DrawText(hint, w / 2 - MeasureText(hint, 22) / 2, GetScreenHeight() - 60, 22,
                 Fade(RAYWHITE, 0.6f));
        return;
    }

    const int visible = (GetScreenHeight() - kListTop - kListBottom) / kRowH;
    const int end = std::min(static_cast<int>(entries_.size()), scroll_ + visible);
    for (int i = scroll_; i < end; ++i) {
        const int y = kListTop + (i - scroll_) * kRowH;
        const bool sel = (i == selected_);
        if (sel) {
            DrawRectangle(20, y - 4, w - 40, kRowH - 6, Color{70, 55, 80, 255});
            DrawRectangle(20, y - 4, 6, kRowH - 6, GOLD);
        }
        DrawText(entries_[i].label.c_str(), 44, y, 26, sel ? RAYWHITE : Fade(RAYWHITE, 0.7f));
    }

    // 卷軸位置提示
    DrawText(TextFormat("%d / %d", selected_ + 1, static_cast<int>(entries_.size())),
             w - 140, 120, 22, GRAY);

    const char* hint = "UP/DOWN  select      ENTER  play      ESC  quit";
    DrawText(hint, w / 2 - MeasureText(hint, 22) / 2, GetScreenHeight() - 55, 22,
             Fade(RAYWHITE, 0.6f));
}
