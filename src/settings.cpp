#include <fstream>
#include <sstream>
#include <string>

#include "settings.h"

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

Settings loadSettings(const std::filesystem::path& file) {
    Settings s;
    std::ifstream in(file);
    if (!in.is_open()) return s;  // 沒有設定檔就用預設值

    std::string line;
    while (std::getline(in, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        const std::string key = trim(line.substr(0, pos));
        const std::string val = trim(line.substr(pos + 1));
        if (val.empty()) continue;
        try {
            if (key == "audioOffsetMs") {
                s.audioOffsetMs = std::stoi(val);
            } else if (key == "scrollSpeed") {
                s.scrollSpeed = std::stof(val);
            } else if (key == "musicVolume") {
                s.musicVolume = std::stof(val);
            } else if (key == "effectVolume") {
                s.effectVolume = std::stof(val);
            } else if (key == "noteScale") {
                s.noteScale = std::stof(val);
            } else if (key == "noteShape") {
                s.noteShape = std::stoi(val);
            } else if (key == "roundNotes") {  // 舊版相容
                s.noteShape = (std::stoi(val) != 0) ? 1 : 0;
            } else if (key == "keys") {
                std::stringstream ss(val);
                std::string tok;
                int i = 0;
                while (i < 7 && std::getline(ss, tok, ',')) {
                    s.keys[i++] = std::stoi(trim(tok));
                }
            } else if (key == "keys4") {
                std::stringstream ss(val);
                std::string tok;
                int i = 0;
                while (i < 4 && std::getline(ss, tok, ',')) {
                    s.keys4[i++] = std::stoi(trim(tok));
                }
            }
        } catch (...) {
            // 忽略損壞的數值，保留預設
        }
    }
    return s;
}

void saveSettings(const Settings& s, const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::trunc);
    if (!out.is_open()) return;
    out << "audioOffsetMs=" << s.audioOffsetMs << "\n";
    out << "scrollSpeed=" << s.scrollSpeed << "\n";
    out << "musicVolume=" << s.musicVolume << "\n";
    out << "effectVolume=" << s.effectVolume << "\n";
    out << "noteScale=" << s.noteScale << "\n";
    out << "noteShape=" << s.noteShape << "\n";
    out << "keys=";
    for (int i = 0; i < 7; ++i) out << (i ? "," : "") << s.keys[i];
    out << "\n";
    out << "keys4=";
    for (int i = 0; i < 4; ++i) out << (i ? "," : "") << s.keys4[i];
    out << "\n";
}
