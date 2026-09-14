# OverKey

[![CI](https://github.com/Young-TW/OverKey/actions/workflows/ci.yml/badge.svg)](https://github.com/Young-TW/OverKey/actions/workflows/ci.yml)

A falling-rhythm game (osu!mania-style) in C++20, with **two frontends sharing one
logic core**: a graphical version (raylib) and a terminal version (TUI). Supports
**4K and 7K** mania beatmaps in the `.osu` format.

```
選歌（自動篩 4K/7K，hover 試聽副歌）→ 倒數 → 下落遊玩（長押、可調速、聲光）
→ 暫停選單 → 結算（評級、誤差直方圖、offset 建議）→ 回選單
```

## Features

- **Two frontends, one core**: judgment, scoring, timing live in a raylib-free core
  (`PlaySession` + `SongClock`); the GUI and TUI are thin shells over it.
- **4K & 7K** mania support (column mapping driven by the beatmap's key count).
- **5-tier judgment**: Perfect / Great / Good / Bad / Miss, with combo and accuracy.
- **Live QR / pp counter** in the gameplay HUD: Quaver rating and osu!mania pp
  estimated from your judgment quality so far, scaled by play progress — starts at
  0, drops on misses, and the value at the last note equals the result screen
  (the Quaver QSS difficulty of the map is computed once per play).
- **Long notes** with press/hold/release judging.
- **Smooth, constant-velocity scroll clock** (frame-delta driven, audio used only as a
  drift anchor — no judder from the quantized audio position).
- **Song select** with metadata panel and **chorus preview** on hover (osu `PreviewTime`).
- **Settings** (persisted to `overkey.cfg`, shared by both frontends): audio offset,
  scroll speed, music/effect volume, per-lane keybinds for 4K and 7K.
- **Result screen**: grade, accuracy, per-tier counts, timing-error histogram, and a
  suggested offset for calibration.
- **Pause menu** (Resume / Retry / Quit), **quick retry**, **live speed adjust**.
- GUI: borderless fullscreen via a virtual-resolution render target (letterboxed).
- TUI: solid eighth-block rendering (rows×8 vertical resolution), Kitty keyboard
  protocol for key-release (needed for long notes), synchronized output for smoothness.

## Download

Prebuilt Linux packages (`.tar.gz` / `.zip` / `.deb`) are attached to each
[GitHub Release](https://github.com/Young-TW/OverKey/releases). They dynamically link
X11/OpenGL/ALSA, so a normal desktop Linux has what they need.

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (GCC ≥ 10 / Clang ≥ 17). All dependencies
(raylib for the GUI; miniaudio + stb for the TUI; miniz for archive unpacking in the
shared core) are fetched automatically via `FetchContent` — no system install needed.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Produces two executables in `build/`:

- `OverKey` — graphical (raylib) frontend
- `overkey-tui` — terminal frontend

The two frontends can be built separately with the `OVERKEY_BUILD_GUI` /
`OVERKEY_BUILD_TUI` options (both `ON` by default):

```bash
cmake -S . -B build -DOVERKEY_BUILD_GUI=OFF   # TUI only
cmake -S . -B build -DOVERKEY_BUILD_TUI=OFF   # GUI only
```

The **GUI** depends on raylib (window/graphics/audio), which needs OpenGL and
X11 to build. The **TUI** is fully headless: its audio comes from
[miniaudio](https://github.com/mackron/miniaudio) and its image handling (for
the Kitty cover art) from [stb](https://github.com/nothings/stb), so a
`-DOVERKEY_BUILD_GUI=OFF` build has **no OpenGL / X11 / raylib dependency** and
compiles on headless machines (e.g. compute clusters). Both fetch their
dependencies automatically; a C++20 compiler (GCC ≥ 10 / Clang ≥ 17) is
required either way.

## Run

```bash
./build/OverKey            # scans ./maps
./build/OverKey /path/to/maps
./build/overkey-tui        # terminal version (needs kitty/ghostty — see below)
```

### Maps

The repo ships one bundled chart, **`maps/OverKey Sample/7K Tutorial.osu`** — a short
7K pattern with no audio or background, so the game has something to play on first
launch. Drop your own beatmaps into `maps/` alongside it (they stay out of git).

Point the game at a folder of beatmaps (4K or 7K); non-mania and other key counts are
filtered out automatically. Both formats are supported:

- **osu!mania** `.osu` (archives: `.osz`)
- **Quaver** `.qua` (archives: `.qp`)

**Just drop the archive in.** On launch the game spawns a background thread that scans
`maps/` for `.osz`/`.qp` packages and unpacks each into a sibling folder (`My Song.osz`
→ `My Song/`). It's **incremental**: every package appears in the song list the moment it
finishes extracting — no need to restart, and you can drop archives in before launch. The
original archive is left untouched, and a package whose folder already exists is skipped
(no re-extract, no overwrite), so it's safe to relaunch.

You can still extract by hand if you prefer:

```bash
unzip "Some Song.osz" -d maps/SomeSong    # osu
unzip "12345.qp"      -d maps/SomeQuaver  # quaver
```

> osu! **lazer** stores beatmaps in a Realm database + hashed content store, so its
> folder is not directly readable — use the bundled importer instead (see below).

### Import from osu!(lazer) & Quaver — `tools/map-import/`

A small Node.js tool that pulls your **existing local** libraries into `maps/` —
no re-downloading, no manual exporting:

```bash
cd tools/map-import
npm install        # one-time (fetches the realm package for reading lazer's DB)
node import.js
```

- **osu!(lazer)**: reads `client.realm` read-only (dynamic schema, never touches
  the DB) and materialises each mania beatmap set as `maps/Lazer/<OnlineID> <Artist> - <Title>/`.
  Use `--all` to import every ruleset, not just mania.
- **Quaver**: finds your Steam install (all libraries, incl. Flatpak; honors a
  custom `SongDirectory` in `quaver.cfg`) and mirrors every song folder into
  `maps/Quaver/`.
- Files are **hard-linked** (zero extra disk; `--copy` to force copying), existing
  folders are skipped, and lazer-side deletions are respected — so re-running it
  after downloading new maps is instant and safe. `--dry-run` previews, `--maps`
  changes the target, `--no-lazer` / `--no-quaver` skip a source.

If your npm blocks install scripts and `require("realm")` fails afterwards, run
`npm install-scripts approve realm && npm rebuild realm` once inside
`tools/map-import/`.

### Controls

| | GUI | TUI |
|---|---|---|
| Lanes (7K default) | `S D F Space J K L` | same |
| Lanes (4K default) | `D F J K` | same |
| Pause | `Esc` | `Esc` |
| Quick retry | `` ` `` / `~` | `` ` `` / `~` |
| Scroll speed | `3` slower / `4` faster (also F3/F4) | `3` / `4` |
| Settings (from menu) | `Tab` | `Tab` |
| Fullscreen toggle (GUI) | `F11` | — |
| Menu navigate | `↑ ↓`, Enter, Esc | `↑↓` or `j k`, Enter, `q` |

The **TUI requires a terminal that supports the Kitty keyboard protocol** (kitty,
ghostty, …) — key-release events are needed to judge long notes. High-refresh terminals
give the smoothest scrolling.

### Calibration

After a play, the result screen shows your mean timing error and a suggested audio
offset. Set it under `Tab → Audio offset` so judgment lines up with the music.

## Architecture

```
include/play.h, src/play.cpp     core: PlaySession, SongClock, Judgment (no raylib)
include/map.h,  src/map.cpp       .osu parsing: loadBeatmap / loadBeatmapInfo / probeBeatmap
include/map_import.h, src/map_import.cpp  background .osz/.qp unpacker (MapImporter, miniz)
include/settings.*                Settings struct + load/save + GUI settings screen
include/game.h, src/game.cpp      GUI frontend over PlaySession
src/song_select.cpp, src/main.cpp GUI menu + entry point
include/tui.h, src/tui.cpp        terminal layer: raw mode, Kitty input, eighth-block canvas
src/tui_main.cpp                  TUI frontend (menu, gameplay, settings, result)
include/render.h                  GUI virtual-resolution viewport
include/raii.h                    RAII wrappers for raylib window/audio/textures
tests/test_core.cpp               unit tests for the raylib-free core
tools/map-import/                 Node tool: import osu!(lazer) + Quaver libraries into maps/
```

CMake builds an `overkey_core` static library shared by both executables.

## Tests

The logic core (map parsing, judgment, scoring, clock) is covered by headless unit
tests:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```
