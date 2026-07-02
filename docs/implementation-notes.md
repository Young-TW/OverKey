# Implementation notes

Hard-won fixes and design decisions in OverKey — the non-obvious things that
took real debugging, kept here so they aren't rediscovered.

## Smooth scrolling (both frontends)

- **Don't drive the note clock from `GetMusicTimePlayed()`.** The audio position
  only advances in ~75 ms buffer chunks, so notes visibly stutter even at high
  FPS. Advance song time by **frame delta** each frame and use the audio
  position only as a correction anchor.
- **Don't nudge toward the audio position every frame.** Pulling the clock 5 %
  toward the (quantized) audio position each frame produced a ~13 Hz speed
  wobble. `SongClock` runs at **constant velocity** and only hard-resyncs when
  drift exceeds 100 ms. Constant velocity = stable perceived speed.

## TUI rendering & smoothness

- **Synchronized output:** wrap each frame in DEC mode 2026
  (`\e[?2026h` … `\e[?2026l`) so kitty/ghostty present frames atomically (no
  tearing).
- **Pacing:** `sleep_until` on a fixed cadence, never `sleep_for(delta)`
  (which accumulates jitter).
- **Eighth-block canvas:** glyphs U+2581..2588 give `rows*8` vertical
  resolution with full per-cell truecolor; `PixelCanvas` diffs against the
  previous frame and emits only changed cells.

## TUI input (Kitty keyboard protocol)

- Push the protocol with flags `1|2|8` (`\e[>11u`). Flag 8 (report all keys as
  escapes) + flag 2 (event types) are required to get key **release** events for
  plain letters, which long notes need.
- Kitty reports letters as lowercase codepoints; normalize to lowercase when
  matching against `Settings` (which store letter binds uppercase, to match the
  GUI's raylib keycodes).

## Asynchronous loading — the big one

Symptom: hovering a new song in the menu dropped the 1 % low to ~10 FPS, while
staying perfectly smooth on the same song. A per-frame timing log pinned the
cause exactly.

**Root cause:** the preview *adopt* step ran `SeekMusicStream` **on the main
thread**, and **MP3 has no seek table** — the decoder scans/decodes from the
start of the file to the target. Seeking to a chorus point ~44 s in cost
**77–226 ms**, stalling the frame.

Two things the TUI main loop must therefore never do:

1. `LoadMusicStream` / `SeekMusicStream` an MP3 (decode-bound, tens–hundreds of ms).
2. A large `write()` to the terminal (a kitty image is tens of KB; a full PTY
   buffer blocks `write()`).

**Fixes in place:**

- **Background writer thread** — `Terminal::write` only enqueues; a dedicated
  thread drains the queue to stdout. The main loop never blocks on terminal I/O,
  so big kitty image writes are free.
- **Single persistent loader thread** — a queue-based worker
  (`Loader::submit` returns a `future`; `Loader::post` is fire-and-forget)
  replaces per-switch `std::async`, eliminating thread-creation churn. All three
  loads (beatmap info, audio, cover) run on it.
- **Preview load+play+seek all on the loader.** It returns an already-playing,
  already-seeked `Music`; the main thread just swaps it in and hands the old
  stream back to the loader for `UnloadMusicStream` (`MusicRes::release`).
- **Async info + cover.** Info parsing is debounced 80 ms so scrolling doesn't
  spawn work per step. Cover decode/encode/base64 runs on the loader; a cover
  change repaints only the panel region (`PixelCanvas::invalidate`) instead of a
  full-screen `forceRedraw`.

**Debugging heuristic:** if something is "already async" but still drops frames,
suspect main-thread work in the *adopt* step (seek / unload / large write), not
the load itself.

## Headless TUI — raylib-free build (`OVERKEY_HEADLESS`)

The TUI used raylib only for **audio** (music + hit sounds) and **image**
handling (Kitty cover art / round-note PNGs). But raylib's CMake always builds
its full GLFW/OpenGL/X11 stack, so even a "TUI only" build failed on headless
machines (`OPENGL_INCLUDE_DIR NOTFOUND` on a compute cluster).

Fix: a small raylib-compatible shim (`include/rl_compat.h` + `src/rl_compat.cpp`)
that reimplements exactly the raylib audio/image API the TUI uses, over
[miniaudio](https://github.com/mackron/miniaudio) (audio) and
[stb](https://github.com/nothings/stb) (`stb_image` / `stb_image_write` /
`stb_image_resize2`, plus `stb_vorbis` for Ogg, which miniaudio doesn't bundle).
Types/signatures match raylib so `tui_main.cpp` only changes its `#include`.
When `OVERKEY_HEADLESS` is defined, `raii.h` pulls the shim instead of
`<raylib.h>` and compiles out the window/texture wrappers. The TUI target links
**no raylib** — its binary needs only libstdc++/m/c (ALSA/PulseAudio are
`dlopen`ed by miniaudio at runtime).

**Bonus:** the shim **fully decodes** each track to PCM up front (mp3/wav/flac
via `ma_decoder`, ogg via stb_vorbis) and plays it through a `ma_audio_buffer` on
the miniaudio engine. That makes `SeekMusicStream` an **O(1) PCM-frame jump** —
the "MP3 has no seek table" stall below simply cannot happen on the TUI anymore
(the async-adopt machinery still stands but the seek itself is now free). Cost:
~30 MB RAM for a decoded 3-minute stereo track, fine anywhere. `UpdateMusicStream`
is a no-op because the engine mixes on its own thread.

## raylib input polling (GUI)

raylib polls input inside `EndDrawing()` (our `vp.end()`).

- **Never `continue` past `EndDrawing`** in the game loop: input state never
  refreshes, so the triggering key reads as "just pressed" every frame — this
  made Esc appear dead and `~` quick-retry freeze in an infinite restart loop.
- **Don't `return` to the menu mid-frame:** the confirming Enter goes
  unconsumed and the next screen sees it as a fresh press (Quit replayed the
  song). Return via a flag *after* `vp.end()`.
- `StopMusicStream` on a **paused** stream doesn't rewind — `ResumeMusicStream`
  first, then stop, so retry restarts audio from 0.
