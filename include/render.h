#ifndef RENDER_H
#define RENDER_H

#include <algorithm>

#include <raylib.h>

// 固定虛擬解析度：所有畫面都以此座標系繪製，再等比縮放到實際視窗。
inline constexpr int kVirtualW = 920;
inline constexpr int kVirtualH = 920;

// 把一格虛擬解析度畫面渲染到 RenderTexture，再 letterbox 置中縮放到實際（全螢幕）視窗。
// 用法：vp.begin(bg); <在虛擬座標系繪製>; vp.end();
class Viewport {
public:
    Viewport() : target_(LoadRenderTexture(kVirtualW, kVirtualH)) {
        SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
    }
    ~Viewport() { UnloadRenderTexture(target_); }
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    void begin(Color bg) {
        BeginTextureMode(target_);
        ClearBackground(bg);
    }

    // 滑鼠游標換算成虛擬座標（end() 的逆變換）；落在 letterbox 黑邊內回傳 false
    bool virtualMouse(Vector2& out) const {
        const float sw = static_cast<float>(GetScreenWidth());
        const float sh = static_cast<float>(GetScreenHeight());
        const float scale = std::min(sw / kVirtualW, sh / kVirtualH);
        const float ox = (sw - kVirtualW * scale) / 2;
        const float oy = (sh - kVirtualH * scale) / 2;
        const Vector2 m = GetMousePosition();
        out = {(m.x - ox) / scale, (m.y - oy) / scale};
        return out.x >= 0 && out.x < kVirtualW && out.y >= 0 && out.y < kVirtualH;
    }

    void end() {
        EndTextureMode();

        const float sw = static_cast<float>(GetScreenWidth());
        const float sh = static_cast<float>(GetScreenHeight());
        const float scale = std::min(sw / kVirtualW, sh / kVirtualH);
        const float w = kVirtualW * scale;
        const float h = kVirtualH * scale;
        const Rectangle src{0, 0, static_cast<float>(kVirtualW),
                            -static_cast<float>(kVirtualH)};  // RenderTexture 上下顛倒
        const Rectangle dst{(sw - w) / 2, (sh - h) / 2, w, h};

        BeginDrawing();
        ClearBackground(BLACK);  // letterbox 黑邊
        DrawTexturePro(target_.texture, src, dst, {0, 0}, 0, WHITE);
        EndDrawing();
    }

private:
    RenderTexture2D target_;
};

#endif
