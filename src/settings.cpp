#include <algorithm>
#include <fstream>
#include <sstream>
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

// 把 keycode 轉成可讀字串
std::string keyName(int code) {
    if (code == KEY_SPACE) return "SPACE";
    if (code >= 33 && code <= 126) return std::string(1, static_cast<char>(code));
    return TextFormat("#%d", code);
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
            if (key == "audioOffsetMs") {
                s.audioOffsetMs = std::stoi(val);
            } else if (key == "scrollSpeed") {
                s.scrollSpeed = std::stof(val);
            } else if (key == "keys") {
                std::stringstream ss(val);
                std::string tok;
                int i = 0;
                while (i < 7 && std::getline(ss, tok, ',')) {
                    s.keys[i++] = std::stoi(trim(tok));
                }
            }
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
    out << "keys=";
    for (int i = 0; i < 7; ++i) out << (i ? "," : "") << s.keys[i];
    out << "\n";
}

void SettingsScreen::run() {
    while (!WindowShouldClose()) {
        constexpr int kFields = 2 + 7;  // offset, speed, 7 keybinds

        if (rebinding_ >= 0) {
            // 等待玩家按任意鍵作為新綁定
            const int k = GetKeyPressed();
            if (k == KEY_ESCAPE) {
                rebinding_ = -1;
            } else if (k != 0) {
                s_.keys[rebinding_] = k;
                rebinding_ = -1;
            }
        } else {
            if (IsKeyPressed(KEY_DOWN)) selected_ = (selected_ + 1) % kFields;
            if (IsKeyPressed(KEY_UP)) selected_ = (selected_ + kFields - 1) % kFields;

            const int dir = IsKeyPressed(KEY_RIGHT) ? 1 : (IsKeyPressed(KEY_LEFT) ? -1 : 0);
            if (selected_ == 0 && dir != 0) {
                s_.audioOffsetMs = std::clamp(s_.audioOffsetMs + dir * 5, -300, 300);
            } else if (selected_ == 1 && dir != 0) {
                s_.scrollSpeed = std::clamp(s_.scrollSpeed + dir * 0.1f, 0.5f, 4.0f);
            } else if (selected_ >= 2 &&
                       (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
                rebinding_ = selected_ - 2;  // 進入重新綁定
            }

            if (IsKeyPressed(KEY_ESCAPE)) return;
        }

        BeginDrawing();
        ClearBackground(Color{18, 18, 24, 255});
        draw();
        EndDrawing();
    }
}

void SettingsScreen::draw() const {
    const int w = GetScreenWidth();
    DrawText("SETTINGS", 40, 50, 48, RAYWHITE);

    auto drawRow = [&](int idx, const char* label, const std::string& value, int y) {
        const bool sel = (idx == selected_);
        if (sel) DrawRectangle(30, y - 6, w - 60, 44, Color{70, 55, 80, 255});
        DrawText(label, 56, y, 26, sel ? RAYWHITE : Fade(RAYWHITE, 0.7f));
        const char* v = value.c_str();
        const bool waiting = (rebinding_ == idx - 2);
        DrawText(v, w - 56 - MeasureText(v, 26), y, 26,
                 waiting ? GOLD : (sel ? GOLD : Fade(RAYWHITE, 0.8f)));
    };

    int y = 140;
    drawRow(0, "Audio offset", TextFormat("%+d ms", s_.audioOffsetMs), y);
    y += 52;
    drawRow(1, "Scroll speed", TextFormat("%.1fx", s_.scrollSpeed), y);
    y += 64;

    DrawText("KEYBINDS", 56, y, 22, Fade(RAYWHITE, 0.55f));
    y += 36;
    for (int i = 0; i < 7; ++i) {
        const std::string val = (rebinding_ == i) ? "press a key..." : keyName(s_.keys[i]);
        drawRow(2 + i, TextFormat("Lane %d", i + 1), val, y);
        y += 48;
    }

    const char* hint =
        "UP/DOWN field   LEFT/RIGHT adjust   ENTER rebind key   ESC save & back";
    DrawText(hint, w / 2 - MeasureText(hint, 20) / 2, GetScreenHeight() - 50, 20,
             Fade(RAYWHITE, 0.6f));
}
