#include "scores.h"

#include <fstream>
#include <sstream>
#include <utility>

// 檔案格式（每行一筆，以 tab 分隔，key 放最後可含空白）：
//   score \t accuracy \t grade \t maxCombo \t key...

ScoreBook::ScoreBook(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in(file_);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        ScoreRecord r;
        std::string scoreStr, accStr, comboStr, key;
        if (!std::getline(ss, scoreStr, '\t')) continue;
        if (!std::getline(ss, accStr, '\t')) continue;
        if (!std::getline(ss, r.grade, '\t')) continue;
        if (!std::getline(ss, comboStr, '\t')) continue;
        if (!std::getline(ss, key)) continue;  // 其餘為 key
        try {
            r.score = std::stoi(scoreStr);
            r.accuracy = std::stod(accStr);
            r.maxCombo = std::stoi(comboStr);
        } catch (...) {
            continue;
        }
        r.valid = true;
        if (!key.empty()) records_[key] = r;
    }
}

ScoreRecord ScoreBook::best(const std::string& key) const {
    const auto it = records_.find(key);
    return it != records_.end() ? it->second : ScoreRecord{};
}

bool ScoreBook::submit(const std::string& key, const ScoreRecord& r) {
    const auto it = records_.find(key);
    if (it != records_.end() && r.score <= it->second.score) return false;
    ScoreRecord rec = r;
    rec.valid = true;
    records_[key] = rec;
    save();
    return true;
}

void ScoreBook::save() const {
    std::ofstream out(file_, std::ios::trunc);
    if (!out.is_open()) return;
    for (const auto& [key, r] : records_) {
        out << r.score << '\t' << r.accuracy << '\t' << r.grade << '\t' << r.maxCombo << '\t'
            << key << '\n';
    }
}
