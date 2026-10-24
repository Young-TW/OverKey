#include <algorithm>
#include <string>

#include <raylib.h>

#include "render.h"
#include "settings.h"

namespace {

// 把 keycode 轉成可讀字串
std::string keyName(int code) {
    if (code == KEY_SPACE) return "SPACE";
    if (code >= 33 && code <= 126) return std::string(1, static_cast<char>(code));
    return TextFormat("#%d", code);
}

// 欄位配置：0-5 數值，之後每種鍵數一段（7K、6K、5K、4K），用四個直欄排開
constexpr int kNumericFields = 6;
constexpr int kKeyCounts[] = {7, 6, 5, 4};      // 各直欄（由左而右）
constexpr int kNumKeyCols = 4;
constexpr int kFieldBase[kNumKeyCols] = {kNumericFields, 6 + 7, 6 + 7 + 6, 6 + 7 + 6 + 5};
constexpr int kTotalFields = kNumericFields + 7 + 6 + 5 + 4;  // 28

int* keyForField(Settings& s, int field) {
    for (int c = 0; c < kNumKeyCols; ++c) {
        if (field >= kFieldBase[c] && field < kFieldBase[c] + kKeyCounts[c]) {
            const int lane = field - kFieldBase[c];
            switch (kKeyCounts[c]) {
                case 7: return &s.keys[lane];
                case 6: return &s.keys6[lane];
                case 5: return &s.keys5[lane];
                default: return &s.keys4[lane];
            }
        }
    }
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
            } else if (selected_ == 5 && dir != 0) {
                s_.noteShape = (s_.noteShape + (dir > 0 ? 1 : 2)) % 3;  // Bar/Round/Round-kitty
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
    y += 42;
    const char* shapeName[] = {"Bar", "Round", "Round (kitty*)"};
    drawRow(5, "Note shape", shapeName[std::clamp(s_.noteShape, 0, 2)], y);
    y += 56;

    // 鍵位區：4 個直欄（7K／6K／5K／4K），欄內由上往下為 Lane 1..N
    constexpr int kColW = 190;
    constexpr int kColX[kNumKeyCols] = {56, 56 + 216, 56 + 432, 56 + 648};
    for (int c = 0; c < kNumKeyCols; ++c) {
        const int kc = kKeyCounts[c];
        const int base = kFieldBase[c];
        DrawText(TextFormat("%dK KEYBINDS", kc), kColX[c], y, 20, Fade(RAYWHITE, 0.55f));
        int ly = y + 30;
        const int* laneKeys = s_.laneKeys(kc).data();
        for (int i = 0; i < kc; ++i) {
            const int field = base + i;
            const bool sel = (field == selected_);
            if (sel) DrawRectangle(kColX[c] - 10, ly - 4, kColW, 32, Color{70, 55, 80, 255});
            DrawText(TextFormat("%d", i + 1), kColX[c], ly, 20, sel ? RAYWHITE : Fade(RAYWHITE, 0.7f));
            const std::string val = (rebinding_ == field) ? "press a key..." : keyName(laneKeys[i]);
            const char* v = val.c_str();
            DrawText(v, kColX[c] + kColW - 16 - MeasureText(v, 20), ly, 20,
                     rebinding_ == field ? GOLD : (sel ? GOLD : Fade(RAYWHITE, 0.8f)));
            ly += 34;
        }
    }

    const char* hint =
        "UP/DOWN field   LEFT/RIGHT adjust   ENTER rebind key   ESC save & back";
    DrawText(hint, w / 2 - MeasureText(hint, 20) / 2, kVirtualH - 44, 20,
             Fade(RAYWHITE, 0.6f));
}
