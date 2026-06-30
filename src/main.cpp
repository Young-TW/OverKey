#include <print>
#include <utility>

#include <raylib.h>

#include "game.h"
#include "map.h"
#include "raii.h"
#include "song_select.h"

namespace {
constexpr int kWindowW = 920;
constexpr int kWindowH = 920;
}  // namespace

int main(int argc, char* argv[]) {
    // 第一個引數可指定 maps 目錄；預設為 ./maps
    std::filesystem::path mapsDir = (argc > 1) ? argv[1] : "maps";

    RaylibApp app{kWindowW, kWindowH, "OverKey"};
    SetExitKey(KEY_NULL);  // 自行處理 ESC，避免它直接關閉視窗

    SongSelect menu{mapsDir};

    while (!WindowShouldClose()) {
        std::optional<std::filesystem::path> choice = menu.run();
        if (!choice) break;  // 玩家離開程式

        Beatmap map = loadBeatmap(*choice);
        if (map.notes.empty()) {
            std::println("沒有讀到任何音符: {}", choice->string());
            continue;
        }

        std::filesystem::path audioPath = choice->parent_path() / map.audioFilename;
        Game game{std::move(map), std::move(audioPath)};
        game.run();  // 結束或中途放棄後回到選單
    }

    return 0;
}
