#ifndef MAP_H
#define MAP_H

#include <string>
#include <fstream>
#include <filesystem>
#include <vector>

struct ManiaNote {
    int column;  // 音軌 (0-6)
    int startTime;
    int endTime; // 若為長押則有值，否則為 -1
};

std::vector<ManiaNote> parse7K(const std::filesystem::path& filename);

#endif
