#include <algorithm>
#include <fstream>
#include <string>

#include <raylib.h>

#include "settings.h"

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

Settings loadSettings(const std::filesystem::path& file) {
    Settings s;
    std::ifstream in(file);
    if (!in.is_open()) return s;  // 沒有設定檔就用預設值

    std::string line;
    while (std::getline(in, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        const std::string key = trim(line.substr(0, pos));
        const std::string val = trim(line.substr(pos + 1));
        if (val.empty()) continue;
        try {
            if (key == "audioOffsetMs") s.audioOffsetMs = std::stoi(val);
            else if (key == "scrollSpeed") s.scrollSpeed = std::stof(val);
        } catch (...) {
            // 忽略損壞的數值，保留預設
        }
    }
    return s;
}

void saveSettings(const Settings& s, const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::trunc);
    if (!out.is_open()) return;
    out << "audioOffsetMs=" << s.audioOffsetMs << "\n";
    out << "scrollSpeed=" << s.scrollSpeed << "\n";
}

void SettingsScreen::run() {
    while (!WindowShouldClose()) {
        constexpr int kFields = 2;
        if (IsKeyPressed(KEY_DOWN)) selected_ = (selected_ + 1) % kFields;
        if (IsKeyPressed(KEY_UP)) selected_ = (selected_ + kFields - 1) % kFields;

        const int dir = IsKeyPressed(KEY_RIGHT) ? 1 : (IsKeyPressed(KEY_LEFT) ? -1 : 0);
        if (dir != 0) {
            if (selected_ == 0) {
                s_.audioOffsetMs += dir * 5;  // 每次 5ms
                s_.audioOffsetMs = std::clamp(s_.audioOffsetMs, -300, 300);
            } else {
                s_.scrollSpeed += dir * 0.1f;
                s_.scrollSpeed = std::clamp(s_.scrollSpeed, 0.5f, 4.0f);
            }
        }

        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER)) return;

        BeginDrawing();
        ClearBackground(Color{18, 18, 24, 255});
        draw();
        EndDrawing();
    }
}

void SettingsScreen::draw() const {
    const int w = GetScreenWidth();
    DrawText("SETTINGS", 40, 60, 48, RAYWHITE);

    struct Row {
        const char* label;
        std::string value;
    };
    const Row rows[2] = {
        {"Audio offset", TextFormat("%+d ms", s_.audioOffsetMs)},
        {"Scroll speed", TextFormat("%.1fx", s_.scrollSpeed)},
    };

    int y = 200;
    for (int i = 0; i < 2; ++i) {
        const bool sel = (i == selected_);
        if (sel) DrawRectangle(30, y - 8, w - 60, 56, Color{70, 55, 80, 255});
        DrawText(rows[i].label, 56, y, 30, sel ? RAYWHITE : Fade(RAYWHITE, 0.7f));
        const char* v = rows[i].value.c_str();
        DrawText(v, w - 56 - MeasureText(v, 30), y, 30, sel ? GOLD : Fade(RAYWHITE, 0.8f));
        y += 80;
    }

    const char* tip =
        "Audio offset：若實機判定整體偏早/晚，依結算的平均誤差調整";
    DrawText(tip, 56, y + 30, 20, Fade(RAYWHITE, 0.55f));

    const char* hint = "UP/DOWN  field      LEFT/RIGHT  adjust      ESC  save & back";
    DrawText(hint, w / 2 - MeasureText(hint, 22) / 2, GetScreenHeight() - 55, 22,
             Fade(RAYWHITE, 0.6f));
}
