#include <cstdio>
#include <utility>

#include <raylib.h>

#include "game.h"
#include "map.h"
#include "raii.h"
#include "render.h"
#include "scores.h"
#include "settings.h"
#include "song_select.h"

namespace {
const char* kConfigFile = "overkey.cfg";
const char* kScoresFile = "overkey-scores.txt";
}  // namespace

int main(int argc, char* argv[]) {
    // 第一個引數可指定 maps 目錄；預設為 ./maps
    std::filesystem::path mapsDir = (argc > 1) ? argv[1] : "maps";

    RaylibApp app{kVirtualW, kVirtualH, "OverKey"};
    SetExitKey(KEY_NULL);          // 自行處理 ESC，避免它直接關閉視窗
    ToggleBorderlessWindowed();    // 啟動即全螢幕（F11 可切換回視窗）

    Viewport viewport;  // 固定虛擬解析度，縮放置中到實際螢幕
    Settings settings = loadSettings(kConfigFile);
    ScoreBook scores{kScoresFile};
    SongSelect menu{mapsDir};

    while (!WindowShouldClose()) {
        MenuResult choice = menu.run(viewport, settings.musicVolume, scores);
        if (choice.action == MenuAction::Quit) break;

        if (choice.action == MenuAction::Settings) {
            SettingsScreen{settings}.run(viewport);
            saveSettings(settings, kConfigFile);
            continue;
        }

        // MenuAction::Play
        Beatmap map = loadBeatmap(choice.path);
        if (map.notes.empty()) {
            std::fprintf(stderr, "沒有讀到任何音符: %s\n", choice.path.string().c_str());
            continue;
        }

        const std::string key = choice.path.string();
        std::filesystem::path audioPath = choice.path.parent_path() / map.audioFilename;
        Game game{std::move(map), std::move(audioPath), settings, scores.best(key)};
        game.run(viewport);  // 結束或中途放棄後回到選單
        if (game.scrollSpeed() != settings.scrollSpeed) {  // F3/F4 調過則存回
            settings.scrollSpeed = game.scrollSpeed();
            saveSettings(settings, kConfigFile);
        }
        if (game.completed()) {  // 打完才記錄成績
            scores.submit(key, {game.finalScore(), game.finalAccuracy(), game.finalGrade(),
                                game.finalMaxCombo(), true});
        }
    }

    return 0;
}
