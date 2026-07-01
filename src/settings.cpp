#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include <raylib.h>

#include "render.h"
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
            } else if (key == "musicVolume") {
                s.musicVolume = std::stof(val);
            } else if (key == "effectVolume") {
                s.effectVolume = std::stof(val);
            } else if (key == "noteScale") {
                s.noteScale = std::stof(val);
            } else if (key == "keys") {
                std::stringstream ss(val);
                std::string tok;
                int i = 0;
                while (i < 7 && std::getline(ss, tok, ',')) {
                    s.keys[i++] = std::stoi(trim(tok));
                }
            } else if (key == "keys4") {
                std::stringstream ss(val);
                std::string tok;
                int i = 0;
                while (i < 4 && std::getline(ss, tok, ',')) {
                    s.keys4[i++] = std::stoi(trim(tok));
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
    out << "musicVolume=" << s.musicVolume << "\n";
    out << "effectVolume=" << s.effectVolume << "\n";
    out << "noteScale=" << s.noteScale << "\n";
    out << "keys=";
    for (int i = 0; i < 7; ++i) out << (i ? "," : "") << s.keys[i];
    out << "\n";
    out << "keys4=";
    for (int i = 0; i < 4; ++i) out << (i ? "," : "") << s.keys4[i];
    out << "\n";
}

// 欄位配置：0-3 數值，4-10 為 7K 鍵位，11-14 為 4K 鍵位
namespace {
constexpr int kNumericFields = 5;
constexpr int k7kBase = kNumericFields;       // 5
constexpr int k4kBase = kNumericFields + 7;   // 12
constexpr int kTotalFields = kNumericFields + 7 + 4;  // 16

int* keyForField(Settings& s, int field) {
    if (field >= k7kBase && field < k4kBase) return &s.keys[field - k7kBase];
    if (field >= k4kBase && field < kTotalFields) return &s.keys4[field - k4kBase];
    return nullptr;
}
}  // namespace

void SettingsScreen::run(Viewport& vp) {
    while (!WindowShouldClose()) {
        if (rebinding_ >= 0) {
            // 等待玩家按任意鍵作為新綁定
            const int k = GetKeyPressed();
            if (k == KEY_ESCAPE) {
                rebinding_ = -1;
            } else if (k != 0) {
                if (int* slot = keyForField(s_, rebinding_)) *slot = k;
                rebinding_ = -1;
            }
        } else {
            if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
            if (IsKeyPressed(KEY_DOWN)) selected_ = (selected_ + 1) % kTotalFields;
            if (IsKeyPressed(KEY_UP)) selected_ = (selected_ + kTotalFields - 1) % kTotalFields;

            const int dir = IsKeyPressed(KEY_RIGHT) ? 1 : (IsKeyPressed(KEY_LEFT) ? -1 : 0);
            if (selected_ == 0 && dir != 0) {
                s_.audioOffsetMs = std::clamp(s_.audioOffsetMs + dir * 5, -300, 300);
            } else if (selected_ == 1 && dir != 0) {
                s_.scrollSpeed = std::clamp(s_.scrollSpeed + dir * 0.1f, 0.5f, 4.0f);
            } else if (selected_ == 2 && dir != 0) {
                s_.musicVolume = std::clamp(s_.musicVolume + dir * 0.05f, 0.0f, 1.0f);
            } else if (selected_ == 3 && dir != 0) {
                s_.effectVolume = std::clamp(s_.effectVolume + dir * 0.05f, 0.0f, 1.0f);
            } else if (selected_ == 4 && dir != 0) {
                s_.noteScale = std::clamp(s_.noteScale + dir * 0.1f, 0.5f, 3.0f);
            } else if (selected_ >= kNumericFields &&
                       (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
                rebinding_ = selected_;  // 進入重新綁定
            }

            if (IsKeyPressed(KEY_ESCAPE)) return;
        }

        vp.begin(Color{18, 18, 24, 255});
        draw();
        vp.end();
    }
}

void SettingsScreen::draw() const {
    const int w = kVirtualW;
    DrawText("SETTINGS", 40, 40, 44, RAYWHITE);

    auto drawRow = [&](int idx, const char* label, const std::string& value, int y) {
        const bool sel = (idx == selected_);
        if (sel) DrawRectangle(30, y - 5, w - 60, 38, Color{70, 55, 80, 255});
        DrawText(label, 56, y, 24, sel ? RAYWHITE : Fade(RAYWHITE, 0.7f));
        const char* v = value.c_str();
        const bool waiting = (rebinding_ == idx);
        DrawText(v, w - 56 - MeasureText(v, 24), y, 24,
                 waiting ? GOLD : (sel ? GOLD : Fade(RAYWHITE, 0.8f)));
    };

    int y = 104;
    drawRow(0, "Audio offset", TextFormat("%+d ms", s_.audioOffsetMs), y);
    y += 42;
    drawRow(1, "Scroll speed", TextFormat("%.1fx", s_.scrollSpeed), y);
    y += 42;
    drawRow(2, "Music volume", TextFormat("%d%%", static_cast<int>(s_.musicVolume * 100)), y);
    y += 42;
    drawRow(3, "Effect volume", TextFormat("%d%%", static_cast<int>(s_.effectVolume * 100)),
            y);
    y += 42;
    drawRow(4, "Note height", TextFormat("%.1fx", s_.noteScale), y);
    y += 50;

    DrawText("7K KEYBINDS", 56, y, 20, Fade(RAYWHITE, 0.55f));
    y += 30;
    for (int i = 0; i < 7; ++i) {
        const std::string val =
            (rebinding_ == k7kBase + i) ? "press a key..." : keyName(s_.keys[i]);
        drawRow(k7kBase + i, TextFormat("Lane %d", i + 1), val, y);
        y += 34;
    }
    y += 16;
    DrawText("4K KEYBINDS", 56, y, 20, Fade(RAYWHITE, 0.55f));
    y += 30;
    for (int i = 0; i < 4; ++i) {
        const std::string val =
            (rebinding_ == k4kBase + i) ? "press a key..." : keyName(s_.keys4[i]);
        drawRow(k4kBase + i, TextFormat("Lane %d", i + 1), val, y);
        y += 34;
    }

    const char* hint =
        "UP/DOWN field   LEFT/RIGHT adjust   ENTER rebind key   ESC save & back";
    DrawText(hint, w / 2 - MeasureText(hint, 20) / 2, kVirtualH - 44, 20,
             Fade(RAYWHITE, 0.6f));
}
