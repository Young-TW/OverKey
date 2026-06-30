#include "tui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <sys/ioctl.h>
#include <unistd.h>

namespace tui {

namespace {

// Braille 每格 2×4 點的位元：[row][col]，col 0=左 1=右
constexpr uint8_t kBrailleBits[4][2] = {
    {0x01, 0x08},
    {0x02, 0x10},
    {0x04, 0x20},
    {0x40, 0x80},
};

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

// ---------------- Terminal ----------------

bool Terminal::isTTY() { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

Terminal::Terminal() {
    if (!isTTY()) return;
    if (tcgetattr(STDIN_FILENO, &orig_) != 0) return;

    termios raw = orig_;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0;   // 非阻塞輪詢
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    ok_ = true;

    refreshSize();
    // 替代螢幕、隱藏游標、推入 Kitty keyboard 旗標（1 區分 + 2 事件類型 + 8 全鍵escape）
    write("\x1b[?1049h\x1b[?25l\x1b[>11u");
}

Terminal::~Terminal() {
    if (!ok_) return;
    write("\x1b[<u\x1b[?25h\x1b[?1049l");  // pop kitty、顯示游標、回主螢幕
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
}

void Terminal::write(const std::string& s) const {
    ssize_t n = ::write(STDOUT_FILENO, s.data(), s.size());
    (void)n;
}

void Terminal::refreshSize() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        rows_ = ws.ws_row;
        cols_ = ws.ws_col;
    }
}

std::vector<KeyEvent> Terminal::poll() {
    std::vector<KeyEvent> events;
    if (!ok_) return events;

    char tmp[512];
    ssize_t n;
    while ((n = read(STDIN_FILENO, tmp, sizeof(tmp))) > 0) {
        buf_.append(tmp, static_cast<size_t>(n));
    }

    size_t i = 0;
    while (i < buf_.size()) {
        const unsigned char c = buf_[i];
        if (c != 0x1b) {  // 一般位元組（如 Ctrl-C 0x03）
            events.push_back({c, KeyEvent::Press});
            ++i;
            continue;
        }
        // ESC：可能是 CSI 序列或孤立 Esc
        if (i + 1 >= buf_.size()) break;  // 不完整，保留尾段
        if (buf_[i + 1] != '[') {
            events.push_back({27, KeyEvent::Press});  // 孤立 Esc
            ++i;
            continue;
        }
        // 找 CSI 終止字元 (0x40-0x7E)
        size_t j = i + 2;
        while (j < buf_.size() && !(buf_[j] >= 0x40 && buf_[j] <= 0x7E)) ++j;
        if (j >= buf_.size()) break;  // 不完整

        const char final = buf_[j];
        const std::string body = buf_.substr(i + 2, j - (i + 2));
        if (final == 'u') {
            // Kitty: keycode[:alt][;mods[:event]]
            try {
                const size_t semi = body.find(';');
                std::string keyfield = (semi == std::string::npos) ? body : body.substr(0, semi);
                const size_t kc = keyfield.find(':');
                const int code = std::stoi(keyfield.substr(0, kc));
                int ev = 1;
                if (semi != std::string::npos) {
                    const std::string rest = body.substr(semi + 1);
                    const size_t c2 = rest.find(':');
                    if (c2 != std::string::npos) ev = std::stoi(rest.substr(c2 + 1));
                }
                events.push_back({code, static_cast<KeyEvent::Type>(ev)});
            } catch (...) {
            }
        }
        // 其他 CSI（方向鍵等 legacy）在此忽略
        i = j + 1;
    }
    buf_.erase(0, i);
    return events;
}

// ---------------- BrailleCanvas ----------------

void BrailleCanvas::resize(int cols, int rows) {
    cols_ = std::max(1, cols);
    rows_ = std::max(1, rows);
    mask_.assign(static_cast<size_t>(cols_) * rows_, 0);
    cur_.assign(static_cast<size_t>(cols_) * rows_, Cell{' ', 200, 200, 200});
    prev_.assign(static_cast<size_t>(cols_) * rows_, Cell{0, 0, 0, 0});  // 強制首幀全畫
}

void BrailleCanvas::clear() {
    std::fill(mask_.begin(), mask_.end(), 0);
    std::fill(cur_.begin(), cur_.end(), Cell{' ', 200, 200, 200});
}

void BrailleCanvas::setDot(int dx, int dy, Rgb color) {
    if (dx < 0 || dy < 0 || dx >= dotW() || dy >= dotH()) return;
    const int cx = dx / 2, cy = dy / 4;
    const size_t idx = static_cast<size_t>(cy) * cols_ + cx;
    mask_[idx] |= kBrailleBits[dy % 4][dx % 2];
    Cell& cell = cur_[idx];
    cell.r = color.r;
    cell.g = color.g;
    cell.b = color.b;
}

void BrailleCanvas::fillDots(int dx0, int dy0, int dx1, int dy1, Rgb color) {
    if (dx1 < dx0) std::swap(dx0, dx1);
    if (dy1 < dy0) std::swap(dy0, dy1);
    dx0 = std::max(0, dx0);
    dy0 = std::max(0, dy0);
    dx1 = std::min(dotW() - 1, dx1);
    dy1 = std::min(dotH() - 1, dy1);
    for (int y = dy0; y <= dy1; ++y)
        for (int x = dx0; x <= dx1; ++x) setDot(x, y, color);
}

void BrailleCanvas::putText(int cx, int cy, const std::string& s, Rgb color) {
    if (cy < 0 || cy >= rows_) return;
    int x = cx;
    for (char ch : s) {
        if (x >= cols_) break;
        if (x >= 0) {
            const size_t idx = static_cast<size_t>(cy) * cols_ + x;
            cur_[idx] = Cell{static_cast<uint32_t>(static_cast<unsigned char>(ch)),
                             color.r, color.g, color.b};
            mask_[idx] = 0;  // 文字覆蓋 Braille
        }
        ++x;
    }
}

void BrailleCanvas::flush(std::string& out) {
    out.clear();
    out += "\x1b[?2026h";  // 開始同步輸出：終端機原子換幀，避免半幀撕裂
    int lastR = 255, lastG = 255, lastB = 255;
    bool colorSet = false;
    int curRow = -1, curCol = -1;
    char seq[32];

    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < cols_; ++x) {
            const size_t idx = static_cast<size_t>(y) * cols_ + x;
            Cell cell = cur_[idx];
            if (cell.cp == ' ' && mask_[idx] != 0) cell.cp = 0x2800 + mask_[idx];
            if (cell.cp == 0) cell.cp = ' ';
            if (cell == prev_[idx]) continue;
            prev_[idx] = cell;

            if (y != curRow || x != curCol) {
                std::snprintf(seq, sizeof(seq), "\x1b[%d;%dH", y + 1, x + 1);
                out += seq;
            }
            if (!colorSet || cell.r != lastR || cell.g != lastG || cell.b != lastB) {
                std::snprintf(seq, sizeof(seq), "\x1b[38;2;%d;%d;%dm", cell.r, cell.g, cell.b);
                out += seq;
                lastR = cell.r;
                lastG = cell.g;
                lastB = cell.b;
                colorSet = true;
            }
            appendUtf8(out, cell.cp);
            curRow = y;
            curCol = x + 1;  // 終端機自動右移
        }
    }
    out += "\x1b[?2026l";  // 結束同步輸出
}

}  // namespace tui
