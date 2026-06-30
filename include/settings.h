#ifndef SETTINGS_H
#define SETTINGS_H

#include <array>
#include <filesystem>

struct Settings {
    int audioOffsetMs = 0;     // 加到歌曲時鐘的偏移（正值＝視判定整體延後）
    float scrollSpeed = 1.0f;  // 下落速度倍率（越大越快）
    // 7 軌鍵位（raylib keycode）；預設 S D F SPACE J K L
    std::array<int, 7> keys{83, 68, 70, 32, 74, 75, 76};
};

Settings loadSettings(const std::filesystem::path& file);
void saveSettings(const Settings& s, const std::filesystem::path& file);

// 設定畫面：就地調整，離開時由呼叫端存檔。假設視窗已由外層開啟。
class SettingsScreen {
public:
    explicit SettingsScreen(Settings& settings) : s_(settings) {}
    void run();  // Esc 返回

private:
    void draw() const;

    Settings& s_;
    int selected_ = 0;
    int rebinding_ = -1;  // 正在等待輸入新鍵的軌道索引，-1 = 無
};

#endif
