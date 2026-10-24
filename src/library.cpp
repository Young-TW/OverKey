#include "library.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "pp.h"

const char* sortKeyName(SortKey k) {
    switch (k) {
        case SortKey::Title: return "TITLE";
        case SortKey::Difficulty: return "DIFFICULTY";
        case SortKey::Grade: return "GRADE";
        case SortKey::Length: return "LENGTH";
        case SortKey::Notes: return "NOTES";
    }
    return "";
}

SortKey nextSortKey(SortKey k) {
    return static_cast<SortKey>((static_cast<int>(k) + 1) % (static_cast<int>(SortKey::Notes) + 1));
}

bool sortNeedsInfo(SortKey k) { return k == SortKey::Length || k == SortKey::Notes; }
bool sortNeedsDiff(SortKey k) { return k == SortKey::Difficulty; }

bool keyFilterAllows(int mask, int keyCount) {
    return keyCount >= kKeyFilterMin && keyCount <= kKeyFilterMax &&
           (mask & (1 << keyCount)) != 0;
}

int toggleKeyFilter(int mask, int keyCount) {
    if (keyCount < kKeyFilterMin || keyCount > kKeyFilterMax) return mask;
    const int bit = 1 << keyCount;
    if ((mask & kKeyFilterAll) == bit) return mask;  // 唯一亮著的 → 關掉會篩空，拒絕
    return (mask ^ bit) & kKeyFilterAll;
}

void loadMetrics(const std::filesystem::path& path, MapMetrics& m, bool withDiff) {
    if (!m.info) m.info = loadBeatmapInfo(path);
    if (withDiff && !m.quaverDiff) {
        const Beatmap bm = loadBeatmap(path);
        m.quaverDiff = quaverDifficulty(bm.notes, bm.keyCount, 1.0f);
        m.osuStar = osuEstimateStarRating(*m.quaverDiff);
    }
}

int gradeRank(const ScoreRecord* best) {
    if (!best || !best->valid) return 0;
    if (best->grade == "SS") return 6;
    if (best->grade == "S") return 5;
    if (best->grade == "A") return 4;
    if (best->grade == "B") return 3;
    if (best->grade == "C") return 2;
    return 1;  // D 及其他
}

namespace {

// ASCII 大小寫折疊的字典序（UTF-8 多位元組位元皆 >= 0x80，不受折疊影響）
int ciCompare(const std::string& a, const std::string& b) {
    auto fold = [](char c) {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
    };
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char ca = static_cast<unsigned char>(fold(a[i]));
        const unsigned char cb = static_cast<unsigned char>(fold(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}

int sign(double d) { return d < 0 ? -1 : (d > 0 ? 1 : 0); }

// 該鍵排序所需資料是否缺漏（缺漏者一律墊底）
bool metricsMissing(SortKey k, const SortRow& r) {
    switch (k) {
        case SortKey::Title: return false;
        case SortKey::Difficulty: return !r.metrics->osuStar.has_value();
        case SortKey::Grade: return gradeRank(r.best) == 0;
        case SortKey::Length: return !r.metrics->info.has_value();
        case SortKey::Notes: return !r.metrics->info.has_value();
    }
    return false;
}

// canonical 比較：a 應排 b 前回傳負值。呼叫前保證兩者皆不缺資料。
int canonicalCmp(SortKey k, const SortRow& a, const SortRow& b) {
    switch (k) {
        case SortKey::Title:
            return ciCompare(*a.label, *b.label);  // 等價的 label 再由外層破平局
        case SortKey::Difficulty:
            return sign(*a.metrics->osuStar - *b.metrics->osuStar);
        case SortKey::Grade: {  // 等第佳者在前，同等第 accuracy 高者在前
            const int ra = gradeRank(a.best), rb = gradeRank(b.best);
            if (ra != rb) return rb - ra;
            return sign(b.best->accuracy - a.best->accuracy);
        }
        case SortKey::Length:
            return a.metrics->info->lengthMs - b.metrics->info->lengthMs;
        case SortKey::Notes:
            return a.metrics->info->noteCount - b.metrics->info->noteCount;
    }
    return 0;
}

}  // namespace

bool sortRowLess(SortKey k, bool asc, const SortRow& a, const SortRow& b) {
    const bool am = metricsMissing(k, a), bm = metricsMissing(k, b);
    if (am != bm) return bm;  // 缺資料者墊底（與方向無關）
    if (!am) {
        const int c = canonicalCmp(k, a, b);
        if (c != 0) return asc ? c < 0 : c > 0;
    }
    const int t = ciCompare(*a.label, *b.label);
    if (t != 0) return t < 0;
    return a.label < b.label;  // label 相同才以位址穩定順序
}

std::string groupName(SortKey k, const SortRow& row) {
    switch (k) {
        case SortKey::Title: {
            // ASCII 字母歸 A-Z；數字、CJK 與其他全進 "#"
            const unsigned char c =
                row.label->empty() ? 0 : static_cast<unsigned char>((*row.label)[0]);
            if (c < 0x80 && std::isalpha(c))
                return std::string(1, static_cast<char>(std::toupper(c)));
            return "#";
        }
        case SortKey::Difficulty: {
            if (!row.metrics->osuStar) return "UNRATED";
            const int lo = std::min(static_cast<int>(*row.metrics->osuStar) / 2 * 2, 10);
            if (lo >= 10) return "10+ \xE2\x98\x85";  // ★
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%d - %d \xE2\x98\x85", lo, lo + 2);
            return buf;
        }
        case SortKey::Grade:
            return (row.best && row.best->valid) ? row.best->grade : "UNPLAYED";
        case SortKey::Length: {
            if (!row.metrics->info) return "UNKNOWN";
            const int s = row.metrics->info->lengthMs / 1000;
            if (s < 60) return "0:00 - 0:59";
            if (s < 120) return "1:00 - 1:59";
            if (s < 180) return "2:00 - 2:59";
            if (s < 300) return "3:00 - 4:59";
            return "5:00+";
        }
        case SortKey::Notes: {
            if (!row.metrics->info) return "UNKNOWN";
            const int n = row.metrics->info->noteCount;
            if (n < 500) return "0 - 499";
            if (n < 1000) return "500 - 999";
            if (n < 2000) return "1000 - 1999";
            if (n < 4000) return "2000 - 3999";
            return "4000+";
        }
    }
    return "";
}
