#ifndef TUI_H
#define TUI_H

#include <cstdint>
#include <string>
#include <vector>

#include <termios.h>

namespace tui {

struct Rgb {
    uint8_t r, g, b;
};

// 方向鍵 / 功能鍵以負值表示，避免與 unicode keycode 衝突
inline constexpr int kKeyUp = -1;
inline constexpr int kKeyDown = -2;
inline constexpr int kKeyRight = -3;
inline constexpr int kKeyLeft = -4;
inline constexpr int kKeyF3 = -13;
inline constexpr int kKeyF4 = -14;

// 一次鍵盤事件（透過 Kitty keyboard protocol 取得）
struct KeyEvent {
    int code;  // Kitty keycode（字母為小寫 unicode；Esc=27、Enter=13、Space=32；方向鍵見上）
    enum Type { Press = 1, Repeat = 2, Release = 3 } type;
};

// 終端機 RAII：raw mode、替代螢幕、隱藏游標、啟用 Kitty keyboard protocol。
// 離開時完整還原。需要在真正的 tty 上執行（isTTY() 為 false 時不應使用）。
class Terminal {
public:
    Terminal();
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    static bool isTTY();

    std::vector<KeyEvent> poll();  // 讀取並解析目前可得的輸入
    void write(const std::string& s) const;

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    void refreshSize();

private:
    termios orig_{};
    bool ok_ = false;
    std::string buf_;  // 跨幀保留未解析完的位元組
    int rows_ = 24, cols_ = 80;
};

// 以八分塊字元（▁▂…▇█）提供垂直 8 倍次格解析度的實心畫布。
// 水平單位為「格」，垂直單位為「像素」(= rows*8)，每像素可獨立上色。
// flush() 只輸出與上一幀不同的格子（diff 渲染）。
class PixelCanvas {
public:
    PixelCanvas(int cols, int rows) { resize(cols, rows); }

    void resize(int cols, int rows);
    void clear();  // 清空本幀內容（保留 prev 以供 diff）

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int pxW() const { return cols_; }       // 水平：每格 1 像素
    int pxH() const { return rows_ * 8; }   // 垂直：每格 8 像素

    void setPixel(int cx, int py, Rgb color);
    void fillRect(int cx0, int py0, int cx1, int py1, Rgb color);  // 含端點
    void putText(int cx, int cy, const std::string& s, Rgb color);  // cy 為格列

    // 保留區（格座標，含端點）：flush 不輸出此範圍，供 kitty 圖片顯示不被覆蓋。
    // 傳入 cx1<cx0 代表清除保留區。
    void setReserved(int cx0, int cy0, int cx1, int cy1);
    void forceRedraw();  // 下一次 flush 全部重畫（保留區變動時用）

    void flush(std::string& out);  // 產生 diff 輸出並更新 prev

private:
    struct Cell {
        uint32_t cp = ' ';                 // 顯示的 codepoint
        uint8_t fr = 0, fg = 0, fb = 0;    // 前景色
        uint8_t br = 0, bg = 0, bb = 0;    // 背景色
        bool operator==(const Cell&) const = default;
    };

    int cols_ = 0, rows_ = 0;
    std::vector<uint8_t> on_;       // 每像素是否填色
    std::vector<Rgb> px_;           // 每像素顏色
    std::vector<uint32_t> textCp_;  // 每格文字 codepoint（0 = 無）
    std::vector<Rgb> textColor_;
    std::vector<Cell> prev_;        // 上一幀，用於 diff
    int rcx0_ = 0, rcy0_ = 0, rcx1_ = -1, rcy1_ = -1;  // 保留區（cx1<cx0＝無）
};

// PNG 等二進位轉 base64（kitty graphics 傳輸用）
std::string base64Encode(const unsigned char* data, std::size_t n);

}  // namespace tui

#endif
