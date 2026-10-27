# OverKey

[![CI](https://github.com/Young-TW/OverKey/actions/workflows/ci.yml/badge.svg)](https://github.com/Young-TW/OverKey/actions/workflows/ci.yml)

A falling-rhythm game (osu!mania-style) in C++20 that runs **entirely in the
terminal** (TUI). Supports **4K, 5K, 6K and 7K** mania beatmaps in the `.osu`
and `.qua` formats.

```
選歌（4K–7K 鍵數篩選，hover 試聽副歌）→ 倒數 → 下落遊玩（長押、可調速、聲光）
→ 暫停選單 → 結算（評級、誤差直方圖、offset 建議）→ 回選單
```

## Features

- **Terminal-native**: no window, no OpenGL — pure truecolor TUI with a
  raylib-free logic core (`PlaySession` + `SongClock`) underneath.
- **4K–7K** mania support: 4K/5K/6K/7K beatmaps (column mapping driven by the
  beatmap's key count), each with its own default and rebindable key layout.
- **5-tier judgment**: Perfect / Great / Good / Bad / Miss, with combo and accuracy.
- **Vertical hit-error meter** beside the playfield: an
  osu-style judgment bar drawn vertically — center line is 0 ms, up = early,
  down = late, bands colored per judgment window. Each hit paints a fading tick
  at its signed offset; a white dash marks the recent average error.
- **Live QR / pp counter** in the gameplay HUD: at any moment, Quaver rating and
  osu!mania pp are recomputed over the map prefix played so far — the QSS
  difficulty of the notes from the start up to now, combined with your current
  accuracy (throttled to ~4 Hz). The value at the last note equals the result
  screen.
- **Long notes** with press/hold/release judging.
- **Smooth, constant-velocity scroll clock** (frame-delta driven, audio used only as a
  drift anchor — no judder from the quantized audio position).
- **Song select** with metadata panel and **chorus preview** on hover (osu `PreviewTime`),
  plus **live search** (press `/`, type to filter artist/title/difficulty, `Esc` clears).
- **Key-count filter** (Quaver-style): toggle 4K/5K/6K/7K chips with the number keys
  `4`–`7` to show only beatmaps of those key counts; at least one key count always
  stays on.
- **Sortable / grouped song list**: press `S` to cycle sort keys — title, difficulty
  (Quaver/osu★), best grade, length, note count — and `Shift+S` to flip the direction.
  The list is grouped with headers (`SS`/`S`/`A`…`UNPLAYED`, `0 - 2 ★`/`2 - 4 ★`…, `A`–`Z`,
  time and note buckets); metrics are computed in a background thread with a progress
  indicator, then **cached to `overkey-cache.txt`** (mtime/size validated) together with
  the sort state, so relaunching doesn't recompute.
- **Settings** (persisted to `overkey.cfg`): audio offset,
  scroll speed, music/effect volume, per-lane keybinds for 4K, 5K, 6K and 7K.
- **Result screen**: grade, accuracy, per-tier counts, timing-error histogram, and a
  suggested offset for calibration. The full stats are persisted with your best score —
  press `r` on a map you have played to bring that best run's result screen back up.
- **Pause menu** (Resume / Retry / Quit), **quick retry**, **live speed adjust**.
- Solid eighth-block rendering (rows×8 vertical resolution), Kitty keyboard
  protocol for key-release (needed for long notes), synchronized output for smoothness.

## Download

Prebuilt Linux packages (`.tar.gz` / `.zip` / `.deb`) are attached to each
[GitHub Release](https://github.com/Young-TW/OverKey/releases). They only need
libstdc++/libc; audio is `dlopen`ed at runtime (ALSA/PulseAudio via miniaudio).

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (GCC ≥ 10 / Clang ≥ 17). All dependencies
(miniaudio + stb for audio/image; miniz for archive unpacking in the core) are fetched
automatically via `FetchContent` — no system install needed.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Produces one executable in `build/`:

- `overkey-tui` — the game

The build is fully headless: audio comes from
[miniaudio](https://github.com/mackron/miniaudio) and image handling (for the Kitty
cover art) from [stb](https://github.com/nothings/stb), so there is
**no OpenGL / X11 / windowing dependency** and it compiles on headless machines
(e.g. compute clusters).

## Run

```bash
./build/overkey-tui         # scans ./maps (needs kitty/ghostty — see below)
./build/overkey-tui /path/to/maps
```

### Maps

The repo ships one bundled chart, **`maps/OverKey Sample/7K Tutorial.osu`** — a short
7K pattern with no audio or background, so the game has something to play on first
launch. Drop your own beatmaps into `maps/` alongside it (they stay out of git).

Point the game at a folder of beatmaps (4K–7K); non-mania and other key counts are
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

| | |
|---|---|
| Lanes (7K default) | `S D F Space J K L` |
| Lanes (6K default) | `S D F J K L` |
| Lanes (5K default) | `D F Space J K` |
| Lanes (4K default) | `D F J K` |
| Pause | `Esc` |
| Quick retry | `` ` `` / `~` |
| Scroll speed | `3` slower / `4` faster |
| Settings (from menu) | `Tab` |
| Search maps (in menu) | `/`, Esc clears |
| Key-count filter (in menu) | `4` `5` `6` `7` |
| Sort/group songs | `s` key, `S` order |
| View best-run result (in menu) | `r` |
| Menu navigate | `↑↓` or `j k`, Enter, `q` |

**Requires a terminal that supports the Kitty keyboard protocol** (kitty,
ghostty, …) — key-release events are needed to judge long notes. High-refresh terminals
give the smoothest scrolling.

### Calibration

After a play, the result screen shows your mean timing error and a suggested audio
offset. Set it under `Tab → Audio offset` so judgment lines up with the music.

## Architecture

```
include/play.h, src/play.cpp     core: PlaySession, SongClock, Judgment
include/map.h,  src/map.cpp       .osu parsing: loadBeatmap / loadBeatmapInfo / probeBeatmap
include/library.h, src/library.cpp  song-list sort keys, group buckets, metric loading
include/metric_cache.h, src/metric_cache.cpp  menu state + sort/difficulty metrics disk cache
include/map_import.h, src/map_import.cpp  background .osz/.qp unpacker (MapImporter, miniz)
include/settings.*                Settings struct + load/save
include/tui.h, src/tui.cpp        terminal layer: raw mode, Kitty input, eighth-block canvas
src/tui_main.cpp                  the game (menu, gameplay, settings, result)
include/rl_compat.h, src/rl_compat.cpp  raylib-compatible audio/image shim (miniaudio + stb)
include/raii.h                    RAII wrappers for music/sound resources
tests/test_core.cpp               unit tests for the logic core
tools/map-import/                 Node tool: import osu!(lazer) + Quaver libraries into maps/
```

CMake builds an `overkey_core` static library linked into the `overkey-tui` executable.

## Tests

The logic core (map parsing, judgment, scoring, clock) is covered by headless unit
tests:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```
