#include "map_import.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <utility>

#include "miniz.h"

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isBeatmapFile(const fs::path& p) {
    const std::string e = lower(p.extension().string());
    return e == ".osu" || e == ".qua";
}

// zip 內的檔名可能含 ".." 或絕對路徑；正規化後確認未逸出 destDir。
// 安全時回傳解出的目標路徑，否則回傳空 path。
fs::path safeJoin(const fs::path& destDir, const std::string& entryName) {
    fs::path rel = fs::path(entryName).lexically_normal();
    if (rel.is_absolute()) return {};
    for (const auto& part : rel)
        if (part == "..") return {};
    return destDir / rel;
}

}  // namespace

bool isBeatmapArchive(const fs::path& p) {
    const std::string e = lower(p.extension().string());
    return e == ".osz" || e == ".qp";
}

std::vector<fs::path> extractBeatmapArchive(const fs::path& archive, const fs::path& destDir) {
    std::vector<fs::path> extracted;

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, archive.string().c_str(), 0)) return extracted;

    std::error_code ec;
    fs::create_directories(destDir, ec);

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;

        const fs::path out = safeJoin(destDir, st.m_filename);
        if (out.empty()) continue;  // 拒絕逸出目錄的惡意/畸形項目

        fs::create_directories(out.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&zip, i, out.string().c_str(), 0)) continue;

        if (isBeatmapFile(out)) extracted.push_back(out);
    }

    mz_zip_reader_end(&zip);
    return extracted;
}

MapImporter::MapImporter(fs::path mapsDir) : mapsDir_(std::move(mapsDir)) {}

MapImporter::~MapImporter() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
}

void MapImporter::start() {
    if (started_.exchange(true)) return;  // 只起一次
    thread_ = std::thread([this] { run(); });
}

std::vector<fs::path> MapImporter::drainNewMaps() {
    std::vector<fs::path> out;
    std::lock_guard<std::mutex> lk(mtx_);
    out.swap(newMaps_);
    return out;
}

void MapImporter::run() {
    std::error_code ec;
    if (!fs::is_directory(mapsDir_, ec)) {
        finished_.store(true);
        return;
    }

    // 先蒐集所有壓縮包（掃描期間目錄可能被解壓改動，故先快照清單）。
    std::vector<fs::path> archives;
    for (auto it = fs::recursive_directory_iterator(
             mapsDir_, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (stop_.load()) { finished_.store(true); return; }
        if (it->is_regular_file(ec) && isBeatmapArchive(it->path()))
            archives.push_back(it->path());
    }

    for (const fs::path& archive : archives) {
        if (stop_.load()) break;

        // 解到壓縮包旁的同名資料夾；已存在則視為先前已匯入，跳過（不重複解、不覆蓋）。
        const fs::path destDir = archive.parent_path() / archive.stem();
        if (fs::exists(destDir, ec)) continue;

        std::vector<fs::path> maps = extractBeatmapArchive(archive, destDir);
        if (maps.empty()) continue;

        std::lock_guard<std::mutex> lk(mtx_);
        newMaps_.insert(newMaps_.end(), maps.begin(), maps.end());
    }

    finished_.store(true);
}
