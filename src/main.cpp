#include <print>
#include <iostream>

#include "map.h"

int main(int argc, char* argv[]) {
    std::filesystem::path osuFile = "maps/KRUX - Illusion of Inflict/7K Hyper.osu";  // 請替換為你的 osu!mania 7K 譜面檔案
    auto notes = parse7K(osuFile);

    for (const auto& note : notes) {
        std::cout << "Column: " << note.column
                  << ", Start Time: " << note.startTime
                  << ", End Time: " << (note.endTime == -1 ? "N/A" : std::to_string(note.endTime))
                  << std::endl;
    }
    return 0;
}
