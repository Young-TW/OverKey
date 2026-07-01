#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

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

// ---- Quaver .qua（YAML 子集）----

bool isQua(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e == ".qua";
}

std::string stripQuotes(std::string v) {
    if (v.size() >= 2 && (v.front() == '\'' || v.front() == '"') && v.back() == v.front())
        return v.substr(1, v.size() - 2);
    return v;
}

// "Keys4" → 4、"Keys7" → 7
int quaKeyCount(const std::string& modeVal) {
    std::string digits;
    for (char c : modeVal)
        if (c >= '0' && c <= '9') digits += c;
    try {
        return digits.empty() ? 0 : std::stoi(digits);
    } catch (...) {
        return 0;
    }
}

// 頂層 key（無縮排、非清單項）
bool isTopKey(const std::string& raw) {
    return !raw.empty() && raw[0] != ' ' && raw[0] != '\t' && raw[0] != '-';
}

Beatmap loadBeatmapQua(const std::filesystem::path& filename) {
    Beatmap map;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "無法打開檔案: " << filename << std::endl;
        return map;
    }

    std::string line;
    bool inHit = false;
    int keyCount = 0;
    bool active = false;
    int start = 0, lane = -1, end = -1;
    auto flush = [&] {
        if (active && lane >= 1) {
            const int k = keyCount > 0 ? keyCount : 4;
            map.notes.push_back({std::clamp(lane - 1, 0, k - 1), start, end});
        }
        active = false;
        lane = -1;
        end = -1;
    };

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (isTopKey(line)) {
            flush();
            const std::string t = trim(line);
            if (t.rfind("AudioFile:", 0) == 0) map.audioFilename = stripQuotes(keyValue(t));
            else if (t.rfind("Title:", 0) == 0) map.title = stripQuotes(keyValue(t));
            else if (t.rfind("Mode:", 0) == 0) keyCount = quaKeyCount(keyValue(t));
            inHit = (t.rfind("HitObjects:", 0) == 0);
            continue;
        }
        if (!inHit) continue;

        std::string t = trim(line);
        if (t.rfind("- ", 0) == 0 || t == "-") {  // 新的 hit object
            flush();
            active = true;
            t = trim(t.substr(1));
            if (t.empty()) continue;
        }
        if (!active) continue;
        if (t.rfind("StartTime:", 0) == 0) {
            try { start = std::stoi(keyValue(t)); } catch (...) {}
        } else if (t.rfind("Lane:", 0) == 0) {
            try { lane = std::stoi(keyValue(t)); } catch (...) {}
        } else if (t.rfind("EndTime:", 0) == 0) {
            try { end = std::stoi(keyValue(t)); } catch (...) {}
        }
    }
    flush();
    if (keyCount > 0) map.keyCount = keyCount;
    return map;
}

BeatmapInfo loadBeatmapInfoQua(const std::filesystem::path& filename) {
    BeatmapInfo info;
    info.mode = 3;  // Quaver 全為 mania
    std::ifstream file(filename);
    if (!file.is_open()) return info;

    std::string line;
    bool inHit = false;
    bool active = false;
    int start = 0, end = -1;
    auto flush = [&] {
        if (active) {
            ++info.noteCount;
            info.lengthMs = std::max(info.lengthMs, std::max(start, end));
        }
        active = false;
        end = -1;
    };

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (isTopKey(line)) {
            flush();
            const std::string t = trim(line);
            if (t.rfind("AudioFile:", 0) == 0) info.audioFilename = stripQuotes(keyValue(t));
            else if (t.rfind("Title:", 0) == 0) info.title = stripQuotes(keyValue(t));
            else if (t.rfind("Artist:", 0) == 0) info.artist = stripQuotes(keyValue(t));
            else if (t.rfind("DifficultyName:", 0) == 0) info.version = stripQuotes(keyValue(t));
            else if (t.rfind("Mode:", 0) == 0) info.keyCount = quaKeyCount(keyValue(t));
            else if (t.rfind("SongPreviewTime:", 0) == 0) {
                try { info.previewTimeMs = std::stoi(keyValue(t)); } catch (...) {}
            }
            inHit = (t.rfind("HitObjects:", 0) == 0);
            continue;
        }
        if (!inHit) continue;
        std::string t = trim(line);
        if (t.rfind("- ", 0) == 0 || t == "-") {
            flush();
            active = true;
            t = trim(t.substr(1));
            if (t.empty()) continue;
        }
        if (!active) continue;
        if (t.rfind("StartTime:", 0) == 0) {
            try { start = std::stoi(keyValue(t)); } catch (...) {}
        } else if (t.rfind("EndTime:", 0) == 0) {
            try { end = std::stoi(keyValue(t)); } catch (...) {}
        }
    }
    flush();
    return info;
}

BeatmapHeader probeBeatmapQua(const std::filesystem::path& filename) {
    BeatmapHeader h;
    h.mode = 3;  // Quaver 全為 mania
    std::ifstream file(filename);
    if (!file.is_open()) return h;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.rfind("Mode:", 0) == 0) h.keyCount = quaKeyCount(keyValue(t));
        else if (t.rfind("HitObjects:", 0) == 0) break;  // 檔頭讀完
    }
    return h;
}

}  // namespace

// 解析整張譜面（依副檔名分派 osu / quaver）
Beatmap loadBeatmap(const std::filesystem::path& filename) {
    if (isQua(filename)) return loadBeatmapQua(filename);
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

            // x 值依鍵數對應到音軌，clamp 避免邊界（x==512）越界
            const int k = (keyCount > 0) ? keyCount : 7;
            const int column = std::clamp((x * k) / 512, 0, k - 1);

            map.notes.push_back({column, time, endTime});
        }
    }

    if (keyCount > 0) map.keyCount = keyCount;
    return map;
}

std::vector<ManiaNote> parse7K(const std::filesystem::path& filename) {
    return loadBeatmap(filename).notes;
}

BeatmapInfo loadBeatmapInfo(const std::filesystem::path& filename) {
    if (isQua(filename)) return loadBeatmapInfoQua(filename);
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
            } else if (trimmed.rfind("AudioFilename", 0) == 0) {
                info.audioFilename = keyValue(trimmed);
            } else if (trimmed.rfind("PreviewTime", 0) == 0) {
                try {
                    info.previewTimeMs = std::stoi(keyValue(trimmed));
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

std::filesystem::path findHitSound(const std::filesystem::path& mapDir) {
    // 依 sample set 優先序找主打擊取樣；raylib 支援 wav/ogg/mp3/flac
    const char* stems[] = {"normal-hitnormal", "soft-hitnormal", "drum-hitnormal", "hitnormal"};
    const char* exts[] = {".wav", ".ogg", ".mp3", ".flac"};
    std::error_code ec;
    for (const char* stem : stems) {
        for (const char* ext : exts) {
            const std::filesystem::path p = mapDir / (std::string(stem) + ext);
            if (std::filesystem::exists(p, ec)) return p;
        }
    }
    return {};
}

BeatmapHeader probeBeatmap(const std::filesystem::path& filename) {
    if (isQua(filename)) return probeBeatmapQua(filename);
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
