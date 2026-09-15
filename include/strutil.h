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

}  // namespace strutil

#endif
