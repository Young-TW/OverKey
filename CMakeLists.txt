cmake_minimum_required(VERSION 3.20.0)
project(OverKey VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# raylib：視窗/2D繪圖/音訊/輸入框架，用 FetchContent 直接拉，免系統安裝
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# 前端無關的遊玩 core（GUI 與 TUI 共用）。settings.cpp 含 raylib 設定畫面，故連結 raylib。
add_library(overkey_core STATIC
    src/map.cpp
    src/play.cpp
    src/settings.cpp
)
target_include_directories(overkey_core PUBLIC ./include)
target_link_libraries(overkey_core PUBLIC raylib)

# GUI 前端（raylib 視窗）
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/game.cpp
    src/song_select.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE overkey_core)

# TUI 前端（終端機；raylib 僅用於音訊，免視窗）
add_executable(overkey-tui
    src/tui_main.cpp
    src/tui.cpp
)
target_link_libraries(overkey-tui PRIVATE overkey_core)

install(TARGETS ${PROJECT_NAME} overkey-tui)

# 測試：只編 raylib-free 的 core 原始碼，headless 可跑
enable_testing()
add_executable(test_core tests/test_core.cpp src/map.cpp src/play.cpp)
target_include_directories(test_core PRIVATE ./include)
add_test(NAME core COMMAND test_core)
