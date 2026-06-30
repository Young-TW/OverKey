#include <print>

#include "game.h"
#include "map.h"

int main(int argc, char* argv[]) {
    // 第一個引數為譜面路徑；未提供則用預設範例
    std::filesystem::path osuFile =
        (argc > 1) ? argv[1] : "maps/KRUX - Illusion of Inflict/7K Hyper.osu";

    Beatmap map = loadBeatmap(osuFile);
    std::println("譜面: {} | 音符數: {}", map.title, map.notes.size());

    if (map.notes.empty()) {
        std::println("沒有讀到任何音符，請確認檔案: {}", osuFile.string());
        return 1;
    }

    // 音訊檔相對於 .osu 所在目錄
    std::filesystem::path audioPath = osuFile.parent_path() / map.audioFilename;

    Game game{std::move(map), std::move(audioPath)};
    game.run();
    return 0;
}
