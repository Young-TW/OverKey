#ifndef MAP_IMPORT_H
#define MAP_IMPORT_H

#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

// 判斷副檔名是否為支援的壓縮圖譜包（osu! .osz / Quaver .qp，皆為 zip）。
bool isBeatmapArchive(const std::filesystem::path& p);

// 將單一壓縮圖譜包解壓到 destDir（自動建立子目錄，防止 zip 路徑穿越）。
// 回傳解出的譜面檔（.osu / .qua）絕對路徑；失敗回傳空 vector。
std::vector<std::filesystem::path> extractBeatmapArchive(
    const std::filesystem::path& archive, const std::filesystem::path& destDir);

// 背景圖譜匯入器：開一條執行緒掃描 mapsDir 底下的 .osz/.qp，逐一解壓，
// 每解完一包就把新譜面檔丟進佇列，讓選單「增量式」即時撈取顯示。
//
// 用法：start() 起執行緒 → 主迴圈每幀呼叫 drainNewMaps() 取新譜面 → 解構時自動 join。
class MapImporter {
public:
    explicit MapImporter(std::filesystem::path mapsDir);
    ~MapImporter();

    MapImporter(const MapImporter&) = delete;
    MapImporter& operator=(const MapImporter&) = delete;

    void start();  // 起背景執行緒（重複呼叫無作用）

    // 取走自上次呼叫以來新解出的譜面檔（非阻塞、執行緒安全）。
    std::vector<std::filesystem::path> drainNewMaps();

    bool finished() const { return finished_.load(); }  // 掃描/解壓全部完成

private:
    void run();

    std::filesystem::path mapsDir_;
    std::thread thread_;
    mutable std::mutex mtx_;
    std::vector<std::filesystem::path> newMaps_;
    std::atomic<bool> started_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> finished_{false};
};

#endif  // MAP_IMPORT_H
