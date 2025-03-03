#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>

#include "map.h"

// 解析 osu!mania 7K 譜面的函式
std::vector<ManiaNote> parse7K(const std::filesystem::path& filename) {
    std::ifstream file(filename);
    std::vector<ManiaNote> notes;
    std::string line;
    bool inHitObjects = false;
    int keyCount = 0;

    if (!file.is_open()) {
        std::cerr << "無法打開檔案: " << filename << std::endl;
        return notes;
    }

    while (std::getline(file, line)) {
        if (line.find("[HitObjects]") != std::string::npos) {
            inHitObjects = true;
            continue;
        }
        if (inHitObjects && line.empty()) break;

        // 取得 Key Count (鍵數)
        if (line.find("CircleSize:") != std::string::npos) {
            keyCount = std::stoi(line.substr(line.find(":") + 1));
            if (keyCount != 7) {
                std::cerr << "這不是 7K 譜面！" << std::endl;
                return {};
            }
        }

        // 解析 HitObjects
        if (inHitObjects) {
            std::stringstream ss(line);
            std::string token;
            std::vector<std::string> tokens;

            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }

            if (tokens.size() < 5) continue;  // 確保資料完整

            int x = std::stoi(tokens[0]);
            int time = std::stoi(tokens[2]);
            int type = std::stoi(tokens[3]);
            int endTime = -1; // 預設為普通 Note

            // 判斷是否為長押
            if (type & 128 && tokens.size() > 5) {
                endTime = std::stoi(tokens[5]);
            }

            // 7K 鍵位範圍：x 值對應列
            int column = (x * 7) / 512;

            notes.push_back({column, time, endTime});
        }
    }

    return notes;
}
