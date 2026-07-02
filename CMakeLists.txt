cmake_minimum_required(VERSION 3.20.0)
project(OverKey VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 兩個前端可分開編譯：-DOVERKEY_BUILD_GUI=OFF 只編 TUI，反之亦然。
option(OVERKEY_BUILD_GUI "Build the GUI frontend (raylib window)" ON)
option(OVERKEY_BUILD_TUI "Build the TUI frontend (terminal)" ON)

if(NOT OVERKEY_BUILD_GUI AND NOT OVERKEY_BUILD_TUI)
    message(FATAL_ERROR
        "至少要開啟一個前端：OVERKEY_BUILD_GUI 或 OVERKEY_BUILD_TUI")
endif()

# 前端無關的遊玩 core（GUI 與 TUI 共用）。純邏輯 + 設定檔讀寫，不依賴 raylib。
add_library(overkey_core STATIC
    src/map.cpp
    src/play.cpp
    src/settings.cpp
    src/scores.cpp
)
target_include_directories(overkey_core PUBLIC ./include)

# raylib：GUI 需視窗/繪圖/音訊，TUI 僅需音訊；兩個前端都要用，用 FetchContent 直接拉。
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL  # 不把 raylib 的 install 規則（.a/標頭）帶進我們的套件
)
FetchContent_MakeAvailable(raylib)

set(OVERKEY_INSTALL_TARGETS "")

# GUI 前端（raylib 視窗）；設定畫面（settings_screen.cpp）僅 GUI 使用。
if(OVERKEY_BUILD_GUI)
    add_executable(${PROJECT_NAME}
        src/main.cpp
        src/game.cpp
        src/song_select.cpp
        src/settings_screen.cpp
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE overkey_core raylib)
    list(APPEND OVERKEY_INSTALL_TARGETS ${PROJECT_NAME})
endif()

# TUI 前端（終端機；raylib 僅用於音訊，免視窗）
if(OVERKEY_BUILD_TUI)
    add_executable(overkey-tui
        src/tui_main.cpp
        src/tui.cpp
    )
    target_link_libraries(overkey-tui PRIVATE overkey_core raylib)
    list(APPEND OVERKEY_INSTALL_TARGETS overkey-tui)
endif()

install(TARGETS ${OVERKEY_INSTALL_TARGETS})
install(FILES README.md DESTINATION share/doc/OverKey)

# 測試：只編 raylib-free 的 core 原始碼，headless 可跑
enable_testing()
add_executable(test_core tests/test_core.cpp src/map.cpp src/play.cpp src/scores.cpp)
target_include_directories(test_core PRIVATE ./include)
add_test(NAME core COMMAND test_core)

# ---- 打包（CPack）----
set(CPACK_PACKAGE_NAME "OverKey")
set(CPACK_PACKAGE_VENDOR "Young-TW")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Falling-rhythm game (osu!mania / Quaver) with GUI and TUI frontends")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Young-TW")
set(CPACK_STRIP_FILES ON)
set(CPACK_GENERATOR "TGZ;ZIP;DEB")
# 執行檔動態連結 X11/GL/ALSA：交給 dpkg-shlibdeps 自動推導 .deb 相依
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Young-TW")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
include(CPack)
