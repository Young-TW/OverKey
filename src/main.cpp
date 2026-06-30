#include <print>
#include <utility>

#include <raylib.h>

#include "game.h"
#include "map.h"
#include "raii.h"
#include "settings.h"
#include "song_select.h"

namespace {
constexpr int kWindowW = 920;
constexpr int kWindowH = 920;
const char* kConfigFile = "overkey.cfg";
}  // namespace

int main(int argc, char* argv[]) {
    // 第一個引數可指定 maps 目錄；預設為 ./maps
    std::filesystem::path mapsDir = (argc > 1) ? argv[1] : "maps";

    RaylibApp app{kWindowW, kWindowH, "OverKey"};
    SetExitKey(KEY_NULL);  // 自行處理 ESC，避免它直接關閉視窗

    Settings settings = loadSettings(kConfigFile);
    SongSelect menu{mapsDir};

    while (!WindowShouldClose()) {
        MenuResult choice = menu.run();
        if (choice.action == MenuAction::Quit) break;

        if (choice.action == MenuAction::Settings) {
            SettingsScreen{settings}.run();
            saveSettings(settings, kConfigFile);
            continue;
        }

        // MenuAction::Play
        Beatmap map = loadBeatmap(choice.path);
        if (map.notes.empty()) {
            std::println("沒有讀到任何音符: {}", choice.path.string());
            continue;
        }

        std::filesystem::path audioPath = choice.path.parent_path() / map.audioFilename;
        Game game{std::move(map), std::move(audioPath), settings};
        game.run();  // 結束或中途放棄後回到選單
    }

    return 0;
}
