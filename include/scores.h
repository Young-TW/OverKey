#ifndef SCORES_H
#define SCORES_H

#include <array>
#include <filesystem>
#include <map>
#include <string>

// 一筆最佳成績
struct ScoreRecord {
    // v2 延伸欄位的尺寸常數（kHistBins 需與 PlaySession::kHistBins 相同）
    static constexpr int kCountFields = 5;  // Perfect/Great/Good/Bad/Miss
    static constexpr int kHistBins = 41;

    int score = 0;
    double accuracy = 0.0;   // 0..100
    std::string grade = "-";
    int maxCombo = 0;
    bool valid = false;      // 是否有實際紀錄

    // ---- v2 延伸（選單回放完整結算畫面用）；舊存檔無此資料時保持預設零值 ----
    std::array<int, kCountFields> counts{};  // 各判定等級次數（Perfect..Miss）
    double meanErrMs = 0.0;                  // 平均有號誤差（>0 = 偏晚）
    int errSamples = 0;                      // 誤差樣本數（0 = 無誤差統計）
    int audioOffsetMs = 0;                   // 成績建立時的 audio offset（suggested 用）
    std::array<int, kHistBins> hist{};       // 誤差直方圖（0ms 居中）
};

// 每譜面（以 .osu 路徑為 key）最佳成績的存檔；純邏輯，無 raylib 依賴。
class ScoreBook {
public:
    explicit ScoreBook(std::filesystem::path file);  // 建構時載入

    // 取得某譜面最佳；無紀錄回傳 valid=false 的預設值
    ScoreRecord best(const std::string& key) const;

    // 若分數高於現有則更新並存檔；回傳是否刷新紀錄
    bool submit(const std::string& key, const ScoreRecord& r);

    void save() const;

private:
    std::filesystem::path file_;
    std::map<std::string, ScoreRecord> records_;
};

#endif
