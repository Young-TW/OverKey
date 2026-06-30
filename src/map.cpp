#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>

#include "map.h"

namespace {

// 去除字串前後空白與 \r（osu 檔常為 CRLF）
std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// 取 "Key: Value" 形式的值
std::string keyValue(const std::string& line) {
    const auto pos = line.find(':');
    if (pos == std::string::npos) return "";
    return trim(line.substr(pos + 1));
}

}  // namespace

// 解析整張 osu!mania 7K 譜面
Beatmap loadBeatmap(const std::filesystem::path& filename) {
    Beatmap map;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "無法打開檔案: " << filename << std::endl;
        return map;
    }

    std::string line;
    std::string section;  // 目前所在的 [Section]
    int keyCount = 0;

    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        // 切換區段
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed;
            continue;
        }

        if (section == "[General]") {
            if (trimmed.rfind("AudioFilename", 0) == 0) {
                map.audioFilename = keyValue(trimmed);
            }
        } else if (section == "[Metadata]") {
            if (trimmed.rfind("Title:", 0) == 0) {
                map.title = keyValue(trimmed);
            }
        } else if (section == "[Difficulty]") {
            if (trimmed.rfind("CircleSize", 0) == 0) {
                keyCount = std::stoi(keyValue(trimmed));
            }
        } else if (section == "[HitObjects]") {
            std::stringstream ss(trimmed);
            std::string token;
            std::vector<std::string> tokens;
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }
            if (tokens.size() < 5) continue;  // 確保資料完整

            const int x = std::stoi(tokens[0]);
            const int time = std::stoi(tokens[2]);
            const int type = std::stoi(tokens[3]);
            int endTime = -1;  // 預設為普通 Note

            // type 第 7 bit (128) 代表 mania 長押；長押的結束時間在 tokens[5] 的 ':' 前
            if ((type & 128) && tokens.size() > 5) {
                const std::string& extra = tokens[5];
                endTime = std::stoi(extra.substr(0, extra.find(':')));
            }

            // 7K 鍵位範圍：x 值對應列，clamp 避免 x==512 算出 7 越界
            const int column = std::clamp((x * 7) / 512, 0, 6);

            map.notes.push_back({column, time, endTime});
        }
    }

    if (keyCount != 0 && keyCount != 7) {
        std::cerr << "警告：這不是 7K 譜面 (CircleSize=" << keyCount << ")" << std::endl;
    }

    return map;
}

std::vector<ManiaNote> parse7K(const std::filesystem::path& filename) {
    return loadBeatmap(filename).notes;
}

BeatmapInfo loadBeatmapInfo(const std::filesystem::path& filename) {
    BeatmapInfo info;
    std::ifstream file(filename);
    if (!file.is_open()) return info;

    std::string line;
    std::string section;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed;
            continue;
        }

        if (section == "[General]") {
            if (trimmed.rfind("Mode:", 0) == 0) {
                try {
                    info.mode = std::stoi(keyValue(trimmed));
                } catch (...) {
                }
            }
        } else if (section == "[Metadata]") {
            if (trimmed.rfind("Title:", 0) == 0) info.title = keyValue(trimmed);
            else if (trimmed.rfind("Artist:", 0) == 0) info.artist = keyValue(trimmed);
            else if (trimmed.rfind("Version:", 0) == 0) info.version = keyValue(trimmed);
        } else if (section == "[Difficulty]") {
            if (trimmed.rfind("CircleSize", 0) == 0) {
                try {
                    info.keyCount = std::stoi(keyValue(trimmed));
                } catch (...) {
                }
            }
        } else if (section == "[HitObjects]") {
            // 只取時間欄位，統計數量與最後時間，不解析完整物件
            const auto t1 = trimmed.find(',');
            if (t1 == std::string::npos) continue;
            const auto t2 = trimmed.find(',', t1 + 1);
            const auto t3 = trimmed.find(',', t2 + 1);
            if (t2 == std::string::npos || t3 == std::string::npos) continue;
            try {
                const int time = std::stoi(trimmed.substr(t2 + 1, t3 - t2 - 1));
                info.lengthMs = std::max(info.lengthMs, time);
                ++info.noteCount;
            } catch (...) {
            }
        }
    }
    return info;
}

BeatmapHeader probeBeatmap(const std::filesystem::path& filename) {
    BeatmapHeader h;
    std::ifstream file(filename);
    if (!file.is_open()) return h;

    std::string line;
    std::string section;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed;
            if (section == "[HitObjects]") break;  // 不需要讀物件清單，提早結束
            continue;
        }
        if (section == "[General]" && trimmed.rfind("Mode:", 0) == 0) {
            try {
                h.mode = std::stoi(keyValue(trimmed));
            } catch (...) {
            }
        } else if (section == "[Difficulty]" && trimmed.rfind("CircleSize", 0) == 0) {
            try {
                h.keyCount = std::stoi(keyValue(trimmed));
            } catch (...) {
            }
        }
    }
    return h;
}
