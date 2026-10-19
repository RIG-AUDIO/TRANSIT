# TRANSIT — CI Visual Testing (GUI smoke harness)

Milestone 3 (C2) deliverable of the `feat/gui-waveform-stability`
assignment. This document describes what the `gui-smoke` CI job actually
validates today, and — equally important — what it does **not** yet check, so
no future reader mistakes phase 1 for full visual verification.

Method: GitHub Actions only (`.github/workflows/linux.yml`, job
`gui-smoke`). The harness never runs locally in this assignment's workflow;
the job itself is the authority.

---

## Why a new tool

`Tools/UiShot.cpp` snapshots `MainComponent` offscreen with no live window
peer and no message pump; its own header notes that JUCE painting silently
writes nothing in that mode on some platforms. The CI gate therefore uses a
**real windowed shell**:

- `xvfb-run -a -s "-screen 0 2560x1440x24"` provides an X display with no
  window manager (fixed window geometry, no WM clamping — deterministic).
- `Tools/GuiSmoke.cpp` creates a chromeless `juce::DocumentWindow`
  (`setUsingNativeTitleBar(false)`, `setTitleBarHeight(0)`,
  `setResizable(false, false)`) hosting the real `MainComponent`, exactly
  like the app shell in `Source/Main.cpp`, but with the content forced to
  each viewport size so every PNG has known dimensions.
- The message loop is serviced explicitly with
  `juce::MessageManager::runDispatchLoopUntil(...)` (the tool defines
  `JUCE_MODAL_LOOPS_PERMITTED=1` for this), so timers, async load
  completion, and repaints actually run between states.
- `AudioEngine` runs with no audio device in CI (its constructor ignores
  device-init failure and the callback is never installed). The
  **playing** state therefore shows transport UI in the playing state with a
  stationary playhead — no device, no clock advance. This is intentional and
  makes the shot deterministic.
- **Bare-server atom seeding.** JUCE 9.0.1's X11 backend interns its
  WM/ICCCM/EWMH atoms (`WM_PROTOCOLS`, `_NET_WM_STATE`, …) with
  `XInternAtom(..., only_if_exists = true)` and passes `WM_PROTOCOLS`
  to `XChangeProperty` unchecked at window creation. On a desktop some other
  client has already created those atoms; on a bare Xvfb server they do not
  exist, the lookup returns 0, and the server answers `BadAtom` (default
  error handler aborts the process). `GuiSmoke` therefore interns every such
  atom name on a short-lived connection first (`seedX11Atoms()`, before
  JUCE's `XWindowSystem` exists) — atoms are server-global, so JUCE's own
  connection finds them.

- **Swallowed X errors with diagnostics.** Seeding is necessary but not
  sufficient: a console harness has no `JUCEApplication`, so
  `JUCEApplicationBase::isStandaloneApp()` is false and JUCE never installs
  the error-swallowing X handler it installs for `START_JUCE_APPLICATION`
  apps (the real TRANSIT binary silently swallows the same residual
  `BadAtom` requests). `GuiSmoke` installs its own handler before JUCE
  initialises: each of the first 20 errors prints code/request/serial/
  resource plus the atom name and a `backtrace_symbols_fd` frame dump to
  stderr (then suppresses), and a swallowed error can never fail the job —
  only missing/invalid screenshots do. The IO handler prints and
  `std::_Exit(1)`s, because a lost X connection cannot produce frames.
  Residual `BadAtom` diagnostics on the green runs are expected output,
  not regressions.

Harness seams are thin wrappers on `MainComponent` (`harnessLoadFile`,
`harnessPlay`, `harnessDeckState`, `harnessSetZoom`, `harnessShowFxPage`)
plus `WaveformView::setZoomIndex`; behavior stays in the engine/view classes.

## What the harness does

1. **Lifecycle cycles (review S11):** three window create/destroy cycles
   before any snapshot, to catch construction/teardown regressions in the
   full shell (engine worker, timer, MIDI, FX scan kick-off).
2. **Synthetic fixtures, generated at test time:** two 8 s, 48 kHz stereo
   PCM16 WAVs (440 Hz / 220 Hz sine + beat envelope) are written into the
   output directory by the harness. No audio fixture is committed to the
   repository.
3. **States × viewports.** Viewports: `1100×824`, `1280×800`, `1440×900`,
   `1920×1080`. States:
   - `empty` — before any file is loaded (pass 1),
   - `error` — deck A failed on a nonexistent path while deck B is still
     empty (G5 state chips: alarm-red `ERROR` + dim `EMPTY` in one frame),
   - `loaded` — both decks `Ready` (poll `harnessDeckState`, 30 s timeout),
   - `zoom4s` — waveform zoom index 0 (4 s window),
   - `whole` — zoom index 7 (whole track),
   - `playing` — transport engaged (see no-audio-device note above),
   - `fx` — FX page shown (`showBrowser(false)`),
   - `hidpi` — loaded state at 2x device scale (component 1280×800,
     PNG 2560×1600; M6 high-DPI sweep, single frame).
   29 PNGs total, named `<state>-<w>x<h>.png` (pixel dimensions; the
   `hidpi` frame is named `hidpi-2560x1600.png`). Meter levels in the
   pass-2 frames come from the `harnessSetMeters` seam (G4), since CI
  has no audio device; the browser folder is seeded via
  `harnessSetBrowserPath(outDir)` (G6), so every frame also shows the
  two fixture rows with metadata columns (name, tempo, duration, folder,
  size, date — the tempo is `—` because the fixture tracks are not in the
  library index) and the selection pill.
4. **Phase-1 validation per frame** (any failure exits non-zero and fails
   the job):
   - file exists and is non-empty,
   - re-decodes as a valid image via `juce::ImageFileFormat::loadFrom`,
   - exact expected pixel dimensions,
   - bright-pixel fraction ≥ 0.1 % (subsampled 4×4 scan, threshold
     `r+g+b > 96`) — catches all-black and all-transparent frames while
     tolerating the dark-glass palette.
5. **Artifacts:** all PNGs are uploaded unconditionally
   (`actions/upload-artifact`, `if: always()`) as `gui-smoke-shots`, so
   failed runs still ship the frames that were produced, for manual review.

## What phase 1 deliberately does NOT check (planned)

These were identified in the review (section C2) and are deferred; they are
listed here rather than silently claimed:

- **Intersection/escape checks** (phase 2): no pixel-level assertions that
  time readouts, status lines, or the wordmark do not collide with or
  escape their containers at each viewport, and no automated
  tab/keyboard-reachability traversal.
- **Reference-image diffing:** no golden screenshots are stored; frames are
  validated structurally (existence, decodability, size, non-blackness) and
  reviewed manually from the artifact bundle.
- **No-WM edge cases:** with no window manager there is no move/resize/drag
  interaction, no fullscreen-transition frame, and no multi-monitor case.
- **Audio-coupled animation:** with no audio device, meter deflection and
  playhead advance are not exercised (see the playing-state note above).

## Running manually

Not part of this assignment's workflow (local builds are out of scope), but
for future contributors the job reproduces as:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target GuiSmoke
xvfb-run -a -s "-screen 0 2560x1440x24" \
  ./build/GuiSmoke_artefacts/Release/GuiSmoke gui-smoke-artifacts
```

(`juce_add_console_app` places binaries under
`<build>/<target>_artefacts/<CONFIG>/`.)

Exit code 0 and a final `GUI_SMOKE ok` line mean all frames passed phase 1.
