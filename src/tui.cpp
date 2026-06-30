#include "tui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <sys/ioctl.h>
#include <unistd.h>

namespace tui {

namespace {

// 下方 n/8 實心塊：n=1→▁ … n=8→█（0x2580+n）
constexpr uint32_t lowerEighth(int n) { return 0x2580u + static_cast<uint32_t>(n); }

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
        // ESC：可能是 CSI 序列、SS3（功能鍵）或孤立 Esc
        if (i + 1 >= buf_.size()) break;  // 不完整，保留尾段
        if (buf_[i + 1] == 'O') {  // SS3：\eOP..\eOS = F1..F4
            if (i + 2 >= buf_.size()) break;
            const char f = buf_[i + 2];
            if (f == 'R') events.push_back({kKeyF3, KeyEvent::Press});
            else if (f == 'S') events.push_back({kKeyF4, KeyEvent::Press});
            i += 3;
            continue;
        }
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
        // 事件類型在參數最後一段 ":event"，預設 1（按下）
        auto eventType = [](const std::string& b) {
            const size_t c = b.rfind(':');
            if (c == std::string::npos) return 1;
            try {
                return std::stoi(b.substr(c + 1));
            } catch (...) {
                return 1;
            }
        };
        if (final == 'u') {
            // Kitty: keycode[:alt][;mods[:event]]
            try {
                const size_t semi = body.find(';');
                std::string keyfield = (semi == std::string::npos) ? body : body.substr(0, semi);
                const size_t kc = keyfield.find(':');
                int code = std::stoi(keyfield.substr(0, kc));
                if (code == 57366) code = kKeyF3;  // Kitty 功能鍵 PUA → 合成碼
                else if (code == 57367) code = kKeyF4;
                const int ev = (semi == std::string::npos) ? 1 : eventType(body);
                events.push_back({code, static_cast<KeyEvent::Type>(ev)});
            } catch (...) {
            }
        } else if (final >= 'A' && final <= 'D') {
            // 方向鍵（含 kitty 事件類型，如 \e[1;1:3A）
            const int code = (final == 'A')   ? kKeyUp
                             : (final == 'B') ? kKeyDown
                             : (final == 'C') ? kKeyRight
                                              : kKeyLeft;
            events.push_back({code, static_cast<KeyEvent::Type>(eventType(body))});
        } else if (final == '~') {
            // CSI tilde 功能鍵：13~=F3、14~=F4（前導數字）
            int num = 0;
            try {
                num = std::stoi(body.substr(0, body.find_first_of(";:")));
            } catch (...) {
            }
            if (num == 13) events.push_back({kKeyF3, static_cast<KeyEvent::Type>(eventType(body))});
            else if (num == 14)
                events.push_back({kKeyF4, static_cast<KeyEvent::Type>(eventType(body))});
        }
        i = j + 1;
    }
    buf_.erase(0, i);
    return events;
}

// ---------------- PixelCanvas ----------------

void PixelCanvas::resize(int cols, int rows) {
    cols_ = std::max(1, cols);
    rows_ = std::max(1, rows);
    const size_t pixels = static_cast<size_t>(cols_) * rows_ * 8;
    on_.assign(pixels, 0);
    px_.assign(pixels, Rgb{0, 0, 0});
    textCp_.assign(static_cast<size_t>(cols_) * rows_, 0);
    textColor_.assign(static_cast<size_t>(cols_) * rows_, Rgb{0, 0, 0});
    prev_.assign(static_cast<size_t>(cols_) * rows_, Cell{1, 1, 1, 1, 1, 1, 1});  // 強制首幀全畫
}

void PixelCanvas::clear() {
    std::fill(on_.begin(), on_.end(), 0);
    std::fill(textCp_.begin(), textCp_.end(), 0);
}

void PixelCanvas::setPixel(int cx, int py, Rgb color) {
    if (cx < 0 || py < 0 || cx >= pxW() || py >= pxH()) return;
    const size_t idx = static_cast<size_t>(py) * cols_ + cx;
    on_[idx] = 1;
    px_[idx] = color;
}

void PixelCanvas::fillRect(int cx0, int py0, int cx1, int py1, Rgb color) {
    if (cx1 < cx0) std::swap(cx0, cx1);
    if (py1 < py0) std::swap(py0, py1);
    cx0 = std::max(0, cx0);
    py0 = std::max(0, py0);
    cx1 = std::min(pxW() - 1, cx1);
    py1 = std::min(pxH() - 1, py1);
    for (int y = py0; y <= py1; ++y)
        for (int x = cx0; x <= cx1; ++x) setPixel(x, y, color);
}

void PixelCanvas::putText(int cx, int cy, const std::string& s, Rgb color) {
    if (cy < 0 || cy >= rows_) return;
    int x = cx;
    for (char ch : s) {
        if (x >= cols_) break;
        if (x >= 0) {
            const size_t idx = static_cast<size_t>(cy) * cols_ + x;
            textCp_[idx] = static_cast<unsigned char>(ch);
            textColor_[idx] = color;
        }
        ++x;
    }
}

void PixelCanvas::flush(std::string& out) {
    out.clear();
    out += "\x1b[?2026h";  // 開始同步輸出：終端機原子換幀，避免半幀撕裂
    bool sgrSet = false;
    int lr = -1, lg = -1, lb = -1, lbr = -1, lbg = -1, lbb = -1;
    int curRow = -1, curCol = -1;
    char seq[48];

    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < cols_; ++x) {
            const size_t cidx = static_cast<size_t>(y) * cols_ + x;
            Cell cell;
            if (textCp_[cidx] != 0) {  // 文字覆蓋整格
                cell.cp = textCp_[cidx];
                const Rgb c = textColor_[cidx];
                cell.fr = c.r; cell.fg = c.g; cell.fb = c.b;
            } else {
                // 取本格 8 個子像素，找填色範圍（音符為連續色帶）
                int first = -1, last = -1;
                Rgb color{0, 0, 0};
                for (int s = 0; s < 8; ++s) {
                    const size_t pidx = static_cast<size_t>(y * 8 + s) * cols_ + x;
                    if (on_[pidx]) {
                        if (first < 0) {
                            first = s;
                            color = px_[pidx];
                        }
                        last = s;
                    }
                }
                if (first < 0) {
                    cell.cp = ' ';
                } else if (first == 0 && last == 7) {  // 整格滿
                    cell.cp = lowerEighth(8);
                    cell.fr = color.r; cell.fg = color.g; cell.fb = color.b;
                } else if (first == 0) {  // 上緣對齊：背景=色，下方挖空
                    cell.cp = lowerEighth(8 - (last + 1));
                    cell.br = color.r; cell.bg = color.g; cell.bb = color.b;  // fg 維持黑
                } else {  // 下緣對齊／中段（近似為下對齊）
                    cell.cp = lowerEighth(last - first + 1);
                    cell.fr = color.r; cell.fg = color.g; cell.fb = color.b;
                }
            }

            if (cell == prev_[cidx]) continue;
            prev_[cidx] = cell;

            if (y != curRow || x != curCol) {
                std::snprintf(seq, sizeof(seq), "\x1b[%d;%dH", y + 1, x + 1);
                out += seq;
            }
            if (!sgrSet || cell.fr != lr || cell.fg != lg || cell.fb != lb ||
                cell.br != lbr || cell.bg != lbg || cell.bb != lbb) {
                std::snprintf(seq, sizeof(seq), "\x1b[38;2;%d;%d;%d;48;2;%d;%d;%dm", cell.fr,
                              cell.fg, cell.fb, cell.br, cell.bg, cell.bb);
                out += seq;
                lr = cell.fr; lg = cell.fg; lb = cell.fb;
                lbr = cell.br; lbg = cell.bg; lbb = cell.bb;
                sgrSet = true;
            }
            appendUtf8(out, cell.cp);
            curRow = y;
            curCol = x + 1;  // 終端機自動右移
        }
    }
    out += "\x1b[0m\x1b[?2026l";  // 重置色彩、結束同步輸出
}

}  // namespace tui
