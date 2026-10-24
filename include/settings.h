#ifndef SETTINGS_H
#define SETTINGS_H

#include <array>
#include <filesystem>
#include <span>

class Viewport;

struct Settings {
    int audioOffsetMs = 0;       // 加到歌曲時鐘的偏移（正值＝視判定整體延後）
    float scrollSpeed = 1.0f;    // 下落速度倍率（越大越快）
    float musicVolume = 0.7f;    // 背景音樂音量 0..1
    float effectVolume = 0.7f;   // 擊打音效音量 0..1
    float noteScale = 1.0f;      // 音符厚度倍率
    int noteShape = 0;           // 0=Bar 1=Round 2=Round(kitty 圖片, 實驗性)
    // 7K 鍵位（raylib keycode）；預設 S D F SPACE J K L
    std::array<int, 7> keys{83, 68, 70, 32, 74, 75, 76};
    // 6K 鍵位；預設 S D F J K L
    std::array<int, 6> keys6{83, 68, 70, 74, 75, 76};
    // 5K 鍵位；預設 D F SPACE J K
    std::array<int, 5> keys5{68, 70, 32, 74, 75};
    // 4K 鍵位；預設 D F J K
    std::array<int, 4> keys4{68, 70, 74, 75};

    // 依鍵數取對應鍵位表（支援 4K–7K）；支援範圍外回傳空 span
    std::span<const int> laneKeys(int keyCount) const {
        switch (keyCount) {
            case 4: return keys4;
            case 5: return keys5;
            case 6: return keys6;
            case 7: return keys;
            default: return {};
        }
    }
};

Settings loadSettings(const std::filesystem::path& file);
void saveSettings(const Settings& s, const std::filesystem::path& file);

// 設定畫面：就地調整，離開時由呼叫端存檔。假設視窗已由外層開啟。
class SettingsScreen {
public:
    explicit SettingsScreen(Settings& settings) : s_(settings) {}
    void run(Viewport& vp);  // Esc 返回

private:
    void draw() const;

    Settings& s_;
    int selected_ = 0;
    int rebinding_ = -1;  // 正在等待輸入新鍵的軌道索引，-1 = 無
};

#endif
