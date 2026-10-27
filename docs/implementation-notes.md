# Implementation notes

Hard-won fixes and design decisions in OverKey — the non-obvious things that
took real debugging, kept here so they aren't rediscovered.

## Smooth scrolling

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
- **Pacing:** hybrid sleep+spin on a fixed cadence (`preciseWaitUntil`), never
  `sleep_for(delta)` (which accumulates jitter). At ~2 ms frame budgets, a pure
  `nanosleep` wake-up can be late by a whole frame — sleep until ~0.5 ms before
  the deadline, then `yield`-spin the rest. Frame target is 500 fps (screen
  ceiling): beyond the terminal's present rate the extra frames are invisible
  anyway, and an achievable target gives the scheduler enough slack that the
  cadence stays steady.
- **Keep O(ms) recomputes off the frame thread.** The live pp/QR counter is a
  full QSS re-run (~2 ms on 6k notes); calling it on the main loop cost one
  guaranteed dropped frame every 250 ms. It now runs on a persistent loader
  thread over a tiny snapshot (judgment counts + song time + the immutable note
  list), with a `busy` flag so jobs never pile up. Same lesson as the async
  loaders below: the frame thread should only render.
- **Make the FPS meter cheap.** Computing avg/1 %/0.1 % lows sorts the whole
  sample window; doing that every 20 frames made the meter itself a periodic
  frametime spike. Once per second is plenty.
- **Eighth-block canvas:** glyphs U+2581..2588 give `rows*8` vertical
  resolution with full per-cell truecolor; `PixelCanvas` diffs against the
  previous frame and emits only changed cells.

## TUI input (Kitty keyboard protocol)

- Push the protocol with flags `1|2|8` (`\e[>11u`). Flag 8 (report all keys as
  escapes) + flag 2 (event types) are required to get key **release** events for
  plain letters, which long notes need.
- Kitty reports letters as lowercase codepoints; normalize to lowercase when
  matching against `Settings` (which store letter binds uppercase).

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

## Headless build — raylib-free audio/image (`OVERKEY_HEADLESS`)

The game needs **audio** (music + hit sounds) and **image** handling (Kitty
cover art / round-note PNGs) without any windowing stack. An earlier
raylib-based build failed on headless machines (`OPENGL_INCLUDE_DIR NOTFOUND`
on a compute cluster) because raylib's CMake always builds its full
GLFW/OpenGL/X11 stack. (Note: the old raylib GUI frontend has since been
removed entirely; the TUI is the only frontend.)

Fix: a small raylib-compatible shim (`include/rl_compat.h` + `src/rl_compat.cpp`)
that reimplements exactly the raylib audio/image API the game uses, over
[miniaudio](https://github.com/mackron/miniaudio) (audio) and
[stb](https://github.com/nothings/stb) (`stb_image` / `stb_image_write` /
`stb_image_resize2`, plus `stb_vorbis` for Ogg, which miniaudio doesn't bundle).
Types/signatures match raylib so the calling code barely changes. With
`OVERKEY_HEADLESS` defined, `raii.h` pulls the shim (it is the only build mode
now). The binary needs only libstdc++/m/c (ALSA/PulseAudio are `dlopen`ed by
miniaudio at runtime).

**Bonus:** the shim **fully decodes** each track to PCM up front (mp3/wav/flac
via `ma_decoder`, ogg via stb_vorbis) and plays it through a `ma_audio_buffer` on
the miniaudio engine. That makes `SeekMusicStream` an **O(1) PCM-frame jump** —
the "MP3 has no seek table" stall below simply cannot happen on the TUI anymore
(the async-adopt machinery still stands but the seek itself is now free). Cost:
~30 MB RAM for a decoded 3-minute stereo track, fine anywhere. `UpdateMusicStream`
is a no-op because the engine mixes on its own thread.

## Audio output latency vs. judgment clock (TUI)

Symptom: "music feels later than the chart" even with `audioOffsetMs=0`, while
osu!/Quaver feel accurate untouched.

- **Cause 1 — device buffering:** miniaudio's default device config gives the
  PulseAudio backend ~19 ms periods × 4 periods of internal queueing (measured
  75 ms). The engine now requests `periodSizeInFrames=240` (5 ms, the
  osu!/Quaver BASS 5–10 ms class). Safe here because every clip is pre-decoded
  PCM, so the audio callback load is trivial.
- **Cause 2 — submission ≠ audible:** the song cursor
  (`ma_sound_get_cursor_in_seconds`) is the *mixer* position; the speakers lag
  it by the whole pipeline (ring queue + PA server buffer + driver). Judging
  against the raw cursor = music systematically late. Fix: estimate the
  pipeline latency at runtime (`GetAudioOutputLatencyMs`, smoothed), **start
  and seek the music early by that much**, and subtract it from the drift
  anchor — the audible beat then lands on the judgment line with offset 0.
  - `pa_stream_get_latency` would be the perfect measurement but miniaudio
    doesn't load it and PA stream calls need the mainloop thread context; we
    compose the estimate from the device internals + the actual `tlength`
    read back from the running stream + one period of driver margin. Residual
    error (±~10 ms on PipeWire) is absorbed by the result-screen calibration.
  - The pulse struct types (`ma_pa_buffer_attr` etc.) live in miniaudio's
    implementation region; declaration-only TUs must duplicate the small ABI
    structs locally.

## Music stop/retry quirk

`StopMusicStream` on a **paused** stream doesn't rewind — resume first, then
stop, so retry restarts audio from 0.
