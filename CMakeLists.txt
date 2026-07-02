cmake_minimum_required(VERSION 3.20.0)
project(OverKey VERSION 0.1.0 LANGUAGES C CXX)

# C++20 就夠（程式沒有真正用到 C++23 語法）。GCC 10.4/11.2 完整支援，
# 老編譯器也不會卡在「不認得 -std=c++23」的錯誤上。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# 兩個前端可分開編譯：-DOVERKEY_BUILD_GUI=OFF 只編 TUI，反之亦然。
#  - GUI 需要 raylib（視窗/繪圖/音訊 → OpenGL/X11）。
#  - TUI 走無頭路線：音訊用 miniaudio、影像用 stb，完全不依賴 raylib/OpenGL/X11，
#    可在叢集等無圖形環境建置。
option(OVERKEY_BUILD_GUI "Build the GUI frontend (raylib window)" ON)
option(OVERKEY_BUILD_TUI "Build the TUI frontend (terminal, headless)" ON)

if(NOT OVERKEY_BUILD_GUI AND NOT OVERKEY_BUILD_TUI)
    message(FATAL_ERROR
        "至少要開啟一個前端：OVERKEY_BUILD_GUI 或 OVERKEY_BUILD_TUI")
endif()

include(FetchContent)

# 前端無關的遊玩 core（GUI 與 TUI 共用）。純邏輯 + 設定檔讀寫，不依賴 raylib。
add_library(overkey_core STATIC
    src/map.cpp
    src/play.cpp
    src/settings.cpp
    src/scores.cpp
)
target_include_directories(overkey_core PUBLIC ./include)

# ---- GUI 前端（raylib 視窗）----
if(OVERKEY_BUILD_GUI)
    # raylib：視窗/2D繪圖/音訊/輸入框架，用 FetchContent 直接拉，免系統安裝
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

    # 設定畫面（settings_screen.cpp）僅 GUI 使用。
    add_executable(${PROJECT_NAME}
        src/main.cpp
        src/game.cpp
        src/song_select.cpp
        src/settings_screen.cpp
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE overkey_core raylib)
endif()

# ---- TUI 前端（終端機，無頭；不連結 raylib）----
if(OVERKEY_BUILD_TUI)
    # miniaudio（音訊）與 stb（影像 + Ogg/Vorbis 解碼），皆為純 CPU 單檔函式庫。
    FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.22
        GIT_SHALLOW TRUE
        # 只需要 miniaudio.h：把 SOURCE_SUBDIR 指向沒有 CMakeLists 的子目錄，
        # 讓 MakeAvailable 只下載、不編 miniaudio 自己那堆範例/節點 target。
        SOURCE_SUBDIR extras
    )
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(miniaudio stb)

    # raylib 相容層：把 TUI 用到的 raylib 音訊/影像 API 以 miniaudio + stb 重新實作。
    add_library(overkey_tui_compat STATIC
        src/vendor/impl_miniaudio.c
        src/vendor/impl_stb.c
        src/vendor/impl_stb_vorbis.c
        src/rl_compat.cpp
    )
    # ./include 對外（tui_main 用 rl_compat.h）；miniaudio/stb 標頭僅本函式庫內部需要。
    target_include_directories(overkey_tui_compat
        PUBLIC ./include
        PRIVATE ${miniaudio_SOURCE_DIR} ${stb_SOURCE_DIR})
    find_package(Threads REQUIRED)
    target_link_libraries(overkey_tui_compat PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

    add_executable(overkey-tui
        src/tui_main.cpp
        src/tui.cpp
    )
    # OVERKEY_HEADLESS：讓 raii.h 改用 rl_compat.h 而非 raylib。
    target_compile_definitions(overkey-tui PRIVATE OVERKEY_HEADLESS)
    target_link_libraries(overkey-tui PRIVATE overkey_core overkey_tui_compat)
endif()

# ---- 安裝 ----
set(OVERKEY_INSTALL_TARGETS "")
if(OVERKEY_BUILD_GUI)
    list(APPEND OVERKEY_INSTALL_TARGETS ${PROJECT_NAME})
endif()
if(OVERKEY_BUILD_TUI)
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
