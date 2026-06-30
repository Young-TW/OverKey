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

// 一次鍵盤事件（透過 Kitty keyboard protocol 取得）
struct KeyEvent {
    int code;  // Kitty keycode（字母為小寫 unicode；Esc=27、Enter=13、Space=32）
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

// 以 Braille（每格 2×4 點）提供次格垂直解析度的畫布；color 為每格一色。
// flush() 只輸出與上一幀不同的格子（diff 渲染）。
class BrailleCanvas {
public:
    BrailleCanvas(int cols, int rows) { resize(cols, rows); }

    void resize(int cols, int rows);
    void clear();  // 清空本幀內容（保留 prev 以供 diff）

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int dotW() const { return cols_ * 2; }
    int dotH() const { return rows_ * 4; }

    void setDot(int dx, int dy, Rgb color);
    void fillDots(int dx0, int dy0, int dx1, int dy1, Rgb color);  // 含端點的點矩形
    void putText(int cx, int cy, const std::string& s, Rgb color);

    void flush(std::string& out);  // 產生 diff 輸出並更新 prev

private:
    struct Cell {
        uint32_t cp;     // 顯示的 codepoint（0 = 空白）
        uint8_t r, g, b;
        bool operator==(const Cell&) const = default;
    };

    int cols_ = 0, rows_ = 0;
    std::vector<uint8_t> mask_;   // 每格 Braille 點陣
    std::vector<Cell> cur_;       // 本幀（文字覆蓋優先於 Braille）
    std::vector<Cell> prev_;      // 上一幀，用於 diff
};

}  // namespace tui

#endif
