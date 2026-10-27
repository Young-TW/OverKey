#ifndef STRUTIL_H
#define STRUTIL_H

#include <cstdint>
#include <string>

// 兩個前端共用的字串小工具（header-only）。比對為位元組級、只折疊 ASCII
// 大小寫：UTF-8 多位元組字元的位元組皆 >= 0x80，不受折疊影響，也不會被
// ASCII needle 誤匹配，因此 CJK 標籤可直接以位元組子字串比對。
namespace strutil {

// 不分大小寫的子字串比對（只折疊 ASCII A-Z；空 needle 恆真）
inline bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto fold = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        for (; j < needle.size(); ++j)
            if (fold(haystack[i + j]) != fold(needle[j])) break;
        if (j == needle.size()) return true;
    }
    return false;
}

// 把 Unicode codepoint 以 UTF-8 附加到字串尾（搜尋框輸入用）
inline void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) return;  // 代理對區段無效
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// 刪除字串尾端一個 UTF-8 字元（Backspace 用）；空字串安全
inline void popUtf8(std::string& s) {
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

// 從 s 的 i 處解碼一個 UTF-8 codepoint，i 推到下一個字元起點。
// 非法序列視為單一位元組並回傳該位元組值（保底可前進，不會死迴圈）。
inline uint32_t nextCodepoint(const std::string& s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    uint32_t cp;
    std::size_t cont;
    if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; cont = 1; }
    else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; cont = 2; }
    else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; cont = 3; }
    else { ++i; return b0; }  // continuation 或 0xF8+：非法開頭
    if (i + cont >= s.size()) { ++i; return b0; }  // 不完整序列
    for (std::size_t k = 1; k <= cont; ++k) {
        const auto bk = static_cast<unsigned char>(s[i + k]);
        if ((bk & 0xC0) != 0x80) { ++i; return b0; }  // 中斷序列
        cp = (cp << 6) | (bk & 0x3F);
    }
    i += cont + 1;
    return cp;
}

// 終端機顯示寬度：CJK 全形/寬字 2 格，其餘 1 格（固定範圍表，不依賴 locale）。
// 常見的東亞寬字區段（East Asian Wide & Fullwidth）；結合符號等寬 0 情況忽略。
inline int displayWidth(uint32_t cp) {
    if ((cp >= 0x1100 && cp <= 0x115F) ||     // Hangul Jamo
        (cp >= 0x2E80 && cp <= 0x303E) ||     // CJK Radicals .. CJK Symbols
        (cp >= 0x3041 && cp <= 0x33FF) ||     // Hiragana .. CJK Compat
        (cp >= 0x3400 && cp <= 0x4DBF) ||     // CJK Ext A
        (cp >= 0x4E00 && cp <= 0x9FFF) ||     // CJK Unified
        (cp >= 0xA000 && cp <= 0xA4CF) ||     // Yi
        (cp >= 0xAC00 && cp <= 0xD7A3) ||     // Hangul Syllables
        (cp >= 0xF900 && cp <= 0xFAFF) ||     // CJK Compat Ideographs
        (cp >= 0xFE30 && cp <= 0xFE4F) ||     // CJK Compat Forms
        (cp >= 0xFF00 && cp <= 0xFF60) ||     // Fullwidth Forms
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||     // Fullwidth signs
        (cp >= 0x20000 && cp <= 0x2FFFD) ||   // CJK Ext B..
        (cp >= 0x30000 && cp <= 0x3FFFD) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF))     // Emoji（多數終端機佔 2 格）
        return 2;
    return 1;
}

// 字串的終端機顯示總格數
inline int displayWidth(const std::string& s) {
    int w = 0;
    for (std::size_t i = 0; i < s.size();) w += displayWidth(nextCodepoint(s, i));
    return w;
}

// 以顯示格數截斷：超過 maxCells 時末尾留 1 格放 "…"；不切在字元中間
inline std::string ellipsizeDisplay(const std::string& s, int maxCells) {
    if (maxCells < 1) return {};
    if (displayWidth(s) <= maxCells) return s;
    std::string out;
    int w = 0;
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t start = i;
        const uint32_t cp = nextCodepoint(s, i);
        const int cw = displayWidth(cp);
        if (w + cw > maxCells - 1) break;
        out.append(s, start, i - start);
        w += cw;
    }
    return out + "\xE2\x80\xA6";  // U+2026 …
}

}  // namespace strutil

#endif
