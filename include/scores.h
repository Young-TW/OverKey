#ifndef SCORES_H
#define SCORES_H

#include <filesystem>
#include <map>
#include <string>

// 一筆最佳成績
struct ScoreRecord {
    int score = 0;
    double accuracy = 0.0;   // 0..100
    std::string grade = "-";
    int maxCombo = 0;
    bool valid = false;      // 是否有實際紀錄
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
