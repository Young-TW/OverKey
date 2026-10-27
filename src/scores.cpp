#include "scores.h"

#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

// 檔案格式（每行一筆，以 tab 分隔，key 放最後可含空白）：
//   v1: score \t accuracy \t grade \t maxCombo \t key...
//   v2: score \t accuracy \t grade \t maxCombo \t v2 \t P,G,Go,B,M \t meanErr \t
//       errSamples \t audioOffset \t h0,...,h40 \t key...
// v2 在 maxCombo 後以 "v2" 標記開頭；讀檔無此標記時整段視為 key（v1 相容）。

namespace {

// 逗號分隔整數 → 固定長度陣列；數量必須恰好相符（例外由呼叫端捕捉）
template <std::size_t N>
bool parseCsvInts(const std::string& s, std::array<int, N>& out) {
    std::istringstream cs(s);
    std::string tok;
    for (std::size_t i = 0; i < N; ++i) {
        if (!std::getline(cs, tok, ',')) return false;
        out[i] = std::stoi(tok);
    }
    return !std::getline(cs, tok, ',');
}

}  // namespace

ScoreBook::ScoreBook(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in(file_);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        ScoreRecord r;
        std::string scoreStr, accStr, comboStr, rest, key;
        if (!std::getline(ss, scoreStr, '\t')) continue;
        if (!std::getline(ss, accStr, '\t')) continue;
        if (!std::getline(ss, r.grade, '\t')) continue;
        if (!std::getline(ss, comboStr, '\t')) continue;
        if (!std::getline(ss, rest)) continue;  // 其餘為 v2 欄位 + key（v1 則整段是 key）
        try {
            r.score = std::stoi(scoreStr);
            r.accuracy = std::stod(accStr);
            r.maxCombo = std::stoi(comboStr);
        } catch (...) {
            continue;
        }

        key = rest;
        if (rest.rfind("v2\t", 0) == 0) {  // v2：延伸欄位以 tab 分隔，key 壓在最後
            std::vector<std::string> tok;
            std::istringstream ts(rest);
            std::string t;
            while (std::getline(ts, t, '\t')) tok.push_back(t);
            // tok[0]="v2"、tok[1..5] 延伸欄位、tok[6..] key（key 含 tab 時會更多）
            bool ok = tok.size() >= 7;
            if (ok) {
                try {
                    ScoreRecord ext;
                    ok = parseCsvInts(tok[1], ext.counts) && parseCsvInts(tok[5], ext.hist);
                    if (ok) {
                        ext.meanErrMs = std::stod(tok[2]);
                        ext.errSamples = std::stoi(tok[3]);
                        ext.audioOffsetMs = std::stoi(tok[4]);
                        r.counts = ext.counts;
                        r.hist = ext.hist;
                        r.meanErrMs = ext.meanErrMs;
                        r.errSamples = ext.errSamples;
                        r.audioOffsetMs = ext.audioOffsetMs;
                    }
                } catch (...) {
                    ok = false;
                }
            }
            if (ok) {
                key = tok[6];
                for (std::size_t i = 7; i < tok.size(); ++i) key += '\t' + tok[i];
            }
            // 延伸欄位毀損：整段當 v1 key（對不上任何譜面路徑，無害）
        }
        if (key.empty()) continue;
        r.valid = true;
        records_[key] = r;
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
        out << r.score << '\t' << r.accuracy << '\t' << r.grade << '\t' << r.maxCombo;
        out << "\tv2\t";
        for (int i = 0; i < ScoreRecord::kCountFields; ++i)
            out << r.counts[i] << (i + 1 < ScoreRecord::kCountFields ? "," : "\t");
        out << r.meanErrMs << '\t' << r.errSamples << '\t' << r.audioOffsetMs << '\t';
        for (int i = 0; i < ScoreRecord::kHistBins; ++i)
            out << r.hist[i] << (i + 1 < ScoreRecord::kHistBins ? "," : "\t");
        out << key << '\n';
    }
}
