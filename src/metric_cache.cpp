#include "metric_cache.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

// 文字欄位不得含 tab/換行（會破壞 tab 分隔格式），以空白取代
std::string sanitize(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return out;
}

}  // namespace

MenuCache::MenuCache(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in(file_);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tag;
        if (!std::getline(ss, tag, '\t')) continue;
        if (tag == "sort") {
            std::string k, a;
            if (std::getline(ss, k, '\t') && std::getline(ss, a, '\t')) {
                try {
                    setSort(std::stoi(k), std::stoi(a) != 0);
                    dirty_ = false;  // 載入的同步不算變更
                } catch (...) {
                }
            }
            continue;
        }
        if (tag == "sel") {  // 取第一個 tab 後的整段剩餘（path 可含 tab）
            std::string rest;
            std::getline(ss, rest);
            setSelected(rest);
            dirty_ = false;
            continue;
        }
        if (tag != "m") continue;
        std::vector<std::string> t;
        for (std::string tok; std::getline(ss, tok, '\t');) t.push_back(std::move(tok));
        // t[0..9] 數值、t[10..14] 文字、t[15..] path（path 含 tab 時會更多）
        if (t.size() < 16) continue;
        Item c;
        int flags = 0;
        try {
            c.mtime = std::stoll(t[0]);
            c.size = std::stoull(t[1]);
            flags = std::stoi(t[2]);
            c.info.mode = std::stoi(t[3]);
            c.info.keyCount = std::stoi(t[4]);
            c.info.lengthMs = std::stoi(t[5]);
            c.info.noteCount = std::stoi(t[6]);
            c.info.previewTimeMs = std::stoi(t[7]);
            c.quaverDiff = std::stod(t[8]);
            c.osuStar = std::stod(t[9]);
        } catch (...) {
            continue;
        }
        c.hasInfo = (flags & 1) != 0;
        c.hasDiff = (flags & 2) != 0;
        c.info.title = t[10];
        c.info.artist = t[11];
        c.info.version = t[12];
        c.info.audioFilename = t[13];
        c.info.backgroundFilename = t[14];
        std::string key = t[15];
        for (std::size_t i = 16; i < t.size(); ++i) key += '\t' + t[i];
        if (!key.empty()) items_[key] = c;
    }
}

void MenuCache::setSort(int key, bool asc) {
    key = std::clamp(key, 0, static_cast<int>(SortKey::Notes));
    if (key == sortKey_ && asc == sortAsc_) return;
    sortKey_ = key;
    sortAsc_ = asc;
    dirty_ = true;
}

void MenuCache::setSelected(const std::string& path) {
    if (path == selectedPath_) return;
    selectedPath_ = path;
    dirty_ = true;
}

bool MenuCache::apply(const std::filesystem::path& path, MapMetrics& m) const {
    const auto it = items_.find(path.string());
    if (it == items_.end()) return false;
    const Item& c = it->second;
    if (c.hasInfo && m.info && c.hasDiff && m.quaverDiff) return false;  // 已齊不用 stat

    std::error_code ec;
    const auto wt = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) return false;
    if (static_cast<std::int64_t>(wt.time_since_epoch().count()) != c.mtime || sz != c.size)
        return false;  // 檔案變動 → 快取失效

    bool filled = false;
    if (c.hasInfo && !m.info) {
        m.info = c.info;
        filled = true;
    }
    if (c.hasDiff && !m.quaverDiff) {
        m.quaverDiff = c.quaverDiff;
        m.osuStar = c.osuStar;
        filled = true;
    }
    return filled;
}

void MenuCache::store(const std::filesystem::path& path, const MapMetrics& m) {
    if (!m.info && !m.quaverDiff) return;
    std::error_code ec;
    const auto wt = std::filesystem::last_write_time(path, ec);
    if (ec) return;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) return;

    Item& c = items_[path.string()];
    const auto count = static_cast<std::int64_t>(wt.time_since_epoch().count());
    if (c.mtime != count || c.size != sz) c = Item{};  // 檔案變動 → 清掉舊欄位再合併
    c.mtime = count;
    c.size = sz;
    if (m.info) {
        c.info = *m.info;
        c.hasInfo = true;
    }
    if (m.quaverDiff) {
        c.quaverDiff = *m.quaverDiff;
        c.osuStar = m.osuStar.value_or(0.0);
        c.hasDiff = true;
    }
    dirty_ = true;
}

void MenuCache::save() {
    if (!dirty_) return;
    std::ofstream out(file_, std::ios::trunc);
    if (!out.is_open()) return;
    out << "sort\t" << sortKey_ << '\t' << (sortAsc_ ? 1 : 0) << '\n';
    if (!selectedPath_.empty()) out << "sel\t" << selectedPath_ << '\n';
    for (const auto& [key, c] : items_) {
        const int flags = (c.hasInfo ? 1 : 0) | (c.hasDiff ? 2 : 0);
        if (flags == 0) continue;
        out << "m\t" << c.mtime << '\t' << c.size << '\t' << flags << '\t' << c.info.mode
            << '\t' << c.info.keyCount << '\t' << c.info.lengthMs << '\t' << c.info.noteCount
            << '\t' << c.info.previewTimeMs << '\t' << c.quaverDiff << '\t' << c.osuStar
            << '\t' << sanitize(c.info.title) << '\t' << sanitize(c.info.artist) << '\t'
            << sanitize(c.info.version) << '\t' << sanitize(c.info.audioFilename) << '\t'
            << sanitize(c.info.backgroundFilename) << '\t' << key << '\n';
    }
    dirty_ = false;
}
