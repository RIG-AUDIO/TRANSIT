# TRANSIT — GUI Rebuild, Waveform & Stability Review

Phase 1 deliverable of the `feat/gui-waveform-stability` assignment.
Baseline reviewed: `main` @ `46dd9ac` (identical to branch point).

Method: **static source review only** — this assignment never compiles or runs
locally; every claim of correctness is validated by GitHub Actions on the exact
commit. Each finding below lists location(s), defect, severity, proposed
change, and the validation we will rely on in CI.

Severity: **C** critical (memory/lifetime corruption class) · **H** high
(correctness users hit) · **M** medium · **L** low/cosmetic-or-racy-but-benign.

---

## 1. Stability findings (Milestone 1)

### S1 — `FxRack::prepare()` and `showEditor()` race the audio thread  (H)
- **Where:** `Source/FxRack.cpp:130-142` (`prepare` calls
  `s.active->prepareToPlay` and `scratchMidi.clear()` on the message thread),
  `Source/FxRack.cpp:103-128` (`showEditor` calls
  `createEditorAndMakeActive()` on the live `active` instance), invoked from
  the 30 Hz timer at `Source/MainComponent.cpp:701-703` whenever
  `rateDirty` (set on every device start, `Source/AudioEngine.cpp:326`).
  The audio thread concurrently calls
  `s.active->processBlock` in `Source/FxRack.cpp:174-189`.
  `stageMutex` does **not** cover audio-side use of `active`.
- **Defect:** classic prepare-vs-process data race on a hosted plugin object;
  `scratchMidi` is likewise cleared while audio may be filling it.
- **Fix:** processing-suspend protocol owned by `FxRack`: message thread sets
  an atomic request flag, audio acknowledges at the next block boundary and
  outputs silence for aux slots while suspended (one atomic load per slot per
  block — no mutex on the RT path); the message thread then performs
  `prepareToPlay` / editor creation safely and releases. Timeout → operation
  fails with an error instead of blocking the message thread indefinitely.
- **Validation:** new lifecycle test driving `processAux` on a fake thread
  against `prepare`/`showEditor` requests (JUCE-free harness around the
  protocol state machine if split out; otherwise CI-only smoke + code
  inspection), sanitizer job (C3).

### S2 — plugin destruction and possible reallocation on the audio thread  (C)
- **Where:** `Source/FxRack.cpp:154-172` (`adoptStaged`):
  `retired.push_back(std::move(s.active))` — `reserve(16)`
  (`Source/FxRack.cpp:6`) is a hope, not a guarantee; >16 retires between
  two `pumpRetired` calls would reallocate on the audio thread. The
  `overflow` path (`Source/FxRack.cpp:165`, comment in
  `Source/FxRack.h:59-63`) destroys the previous occupant **on the audio
  thread** by design.
- **Defect:** allocation and destruction of third-party plugin objects in the
  device callback — the exact class of defect this assignment must remove.
- **Fix:** pure deferral. If the retire lock is unavailable **or**
  `retired.size() == retired.capacity()`, leave `stagedReady` set and retry
  on a later block; delete `overflow` entirely. `pumpRetired` (already
  correct: swap-under-lock, destroy-after-unlock, called from the message
  thread at `Source/MainComponent.cpp:701`) drains the vector, restoring
  capacity. The RT path then never allocates or destroys.
- **Validation:** lifecycle stress test (N staged swaps with a slow pump),
  asserting zero audio-side destruction (instrument via counting deleter in a
  test subclass is not possible without a plugin — use protocol unit test on
  the retire bookkeeping if factored to a helper), sanitizer job, and CI
  code review of the diff.

### S3 — MIDI timestamps never advance  (H)
- **Where:** `Source/AudioEngine.h:295` declares
  `std::atomic<uint64_t> frameCount{0}`; `handleIncomingMidiMessage`
  stamps every event with it (`Source/AudioEngine.cpp:167`) but **nothing
  ever stores to it** — the callback maintains a separate non-atomic
  `blockFrame` (`Source/AudioEngine.cpp:348,356`).
- **Defect:** every event is stamped frame 0, so `MidiQueue::drain`
  (`Source/MidiQueue.h:37-47`) always releases events immediately; the
  frame-accurate control path is dead code, its ordering/deferral semantics
  untested, and §6.4 of the assignment (audit frame-counter vs block-counter)
  fails.
- **Fix:** publish `blockFrame` to `frameCount` at the end of each device
  callback (relaxed store; the MIDI thread's relaxed load observes a
  monotonically non-decreasing value — events stamped ≤ the next drain
  deadline are applied on the next block, never lost, never reordered).
- **Validation:** new JUCE-free `MidiQueue` timing test: stamp events at
  frame F, drain with `uptoFrame < F` → 0 events; drain `>= F` → in order;
  interleaved push/drain; overflow drops newest and counts
  (`Source/MidiQueue.h:27-33`). Plus a queue-level test that a stale stamp
  (≤ current block) still drains (regression guard for the fix).

### S4 — deck buffer & length adoption deferred while paused  (M)
- **Where:** `Source/Deck.cpp:112-116` — `render()` returns on
  `!playing` **before** `adoptStaged()`; the mixer only calls
  `deck.render` for playing (or cueing) strips (`Source/Mixer.cpp:189-197`).
- **Defect:** after loading a track with the transport stopped,
  `infoFrames`/`infoRate` (`Source/Deck.h:105-106`) are not updated, so
  `getLengthSeconds()` and the UI time display stay at the previous track (or
  0) until the first Play press.
- **Fix:** publish `infoFrames`/`infoRate` (already atomics) inside
  `Deck::load` on the message thread; buffer adoption still happens at the
  first render — safe, because no audio is produced while paused.
- **Validation (implemented):** `Tests/DeckLoadTest.cpp` (`deckload_test`)
  loads synthetic tracks while paused and asserts `getNumFrames()` /
  `getLengthSeconds()` without ever calling `render()`, including a paused
  re-load that changes the length and the zero-rate fallback; `null_render`
  and `keylock_test` stay green (AGENTS.md rule 9).

### S5 — synchronous decode/analyze/BPM on the message thread  (M, UX-critical)
- **Where:** `AudioEngine::loadFile` (`Source/AudioEngine.cpp:278-297`)
  performs full decode + `computePeaks` + `detectBpm` inline; callers are
  FileChooser callbacks (`Source/MainComponent.cpp:450-464`, `657-662`) and
  `indexFolder` (`Source/MainComponent.cpp:611-620`).
  Publication members `waves[2]`, `bpm[2]`, `fileLoaded[2]`
  (`Source/AudioEngine.h:280,291,284`) are plain, and `getWaveform` hands
  out a const reference (`Source/AudioEngine.h:147`).
- **Defect:** multi-second UI freezes; worse, the moment loading moves to a
  background thread (required) the plain members become cross-thread races
  and the const-ref waveform read becomes a use-after-free window.
- **Fix (ownership model, implemented on `feat/gui-waveform-stability`):**
  - worker thread owned by `AudioEngine` (started at the end of the ctor,
    joined in the dtor right after `cancelPendingUpdate()` and before the
    device callback is removed); jobs carry `{kind, deck, generation,
    path}` with `lean::LoadGate` — **newest wins**, each pipeline stage
    checks `isCurrent()` and abandons superseded work;
  - the worker **never touches engine state**: it fills a `LoadResult`
    (decoded buffer, peak payload, BPM, filename, error) under
    `resultMutex`, then wakes the message thread via
    `juce::AsyncUpdater::triggerAsyncUpdate()`;
  - `handleAsyncUpdate()` commits on the message thread: publishes
    `Empty/Loading/Analyzing/Ready/Error` (the only cross-thread-readable
    field, `std::atomic<int> states[2]`, polled by the 30 Hz timer and the
    status line), moves the immutable waveform payload into `waves[d]`,
    sets `bpm[d]`, flips `fileLoaded[d]`, stages the audio buffer through
    the unchanged staged-`shared_ptr` pattern in `Deck::load`, then fires
    `onDeckReady` (auto-play + header refresh);
  - `waves`, `bpm`, `fileLoaded`, `names`, `errors` stay **plain** types on
    purpose (deliberate refinement of the earlier `std::atomic<double>`
    suggestion): every write happens inside the message-thread commit and
    every reader (paint, timer) runs on that same thread, so each field is
    observed as a whole old-or-new value — the const-ref `getWaveform` is
    safe by message-thread ownership rather than by a `shared_ptr` copy.
- **Validation (implemented):** `Tests/LoadScheduleTest.cpp`
  (`loadschedule_test`) covers generation supersession, per-deck gate
  independence and concurrent submit/accept monotonicity; the ASan+UBSan
  CI job runs it plus `library_test` (now with concurrent scan+search
  threads) under sanitizers; Loading/Analyzing/Ready/Error transitions are
  driven into the deck status line by the 30 Hz timer; shutdown join is
  covered by dtor ordering (S11).

### S6 — library scan/search synchronous, unserialized, undebounced  (M)
- **Where:** `Library::scanFolder`/`search` (`Source/Library.cpp:100-169`)
  run on the message thread today (`Source/MainComponent.cpp:295` —
  every keystroke runs a SQLite LIKE query; `:620` — full scan from the
  chooser callback); single unguarded `db` handle (`Source/Library.h:36`).
- **Defect:** keystroke latency and folder-scan freezes; once scanning moves
  to a worker, the unguarded handle becomes a data race (SQLITE_MISUSE).
- **Fix:** search debounce (~150 ms timer restart on text change); scan on
  the S5 worker (same shutdown-join); a `std::mutex` serializing all
  `Library` methods (message + worker only — never audio); worker joined
  before `Library` destruction; no queries after close.
- **Validation:** `library_test` extended (concurrent scan+search from two
  threads under sanitizers, deterministic sequencing), CI smoke typing in
  search (visual), shutdown cycle (S11).

### S7 — racy recorder frame counter  (L)
- **Where:** `Recorder::recordedFrames` (`Source/Recorder.h:33`) reads plain
  `totalWritten` (`:54`, writer-thread-only) from the message thread.
- **Fix:** `std::atomic<long long>` (relaxed).
- **Validation:** `recorder_test` stays green; sanitizers.

### S8 — FX prepare uses a hardcoded 1024 block size  (M)
- **Where:** `Source/AudioEngine.h:234` (`fxRack.prepare(rate, 1024)`),
  `Source/FxRack.cpp:45` (instances created with those members).
- **Defect:** on devices with larger buffers (2048), plugins are prepared for
  1024 but receive bigger `processBlock` calls — out of the JUCE hosting
  contract, potential buffer overrun inside third-party code.
- **Fix:** pass `device->getCurrentBufferSizeSamples()` (or the device's
  maximum) through `prepareFx`; keep `S1`'s suspend protocol around the
  re-prepare.
- **Validation:** CI smoke on default device config; note in report.

### S9 — VST3 discovery is Windows-only and synchronous  (M)
- **Where:** `FxRack::scanVst3` (`Source/FxRack.cpp:14-28`) hardcodes
  `C:\Program Files\...` paths; a directory walk on the message thread.
- **Fix (implemented):** platform-aware search paths — Linux: `~/.vst3`,
  `/usr/lib/vst3`, `/usr/local/lib/vst3`, `/usr/lib/x86_64-linux-gnu/vst3`,
  plus `$VST3_PATH` entries (colon-separated); keep Windows paths under
  `#ifdef _WIN32`. The walk moved off the message thread: path collection
  is the JUCE-free `lean::collectVst3Bundles` (`Source/FxScanPaths.h`),
  run as a single-flight job on the S5 worker, cached in
  `AudioEngine::fxPlugins()` (message thread) — `pickAuxPlugin()` shows the
  cache and requests a background refresh. FX view UX stays out of scope
  (API correctness first).
- **Validation (implemented):** `Tests/FxScanTest.cpp` (`fxscan_test`) —
  temp-dir fixture with fake `.vst3` bundle names (files, directories, mixed
  case), non-recursive exclusion, missing-root tolerance, sorted
  `name|path` output; path collection only, no loading, CI-only.

### S10 — aux bypass/empty slots cut tails  (L, documented residual)
- **Where:** `Source/FxRack.cpp:179-183` writes silence for
  bypassed/empty/rate-dirty slots.
- **Decision:** keep the behaviour (host-acceptable, documented in
  `README.md` per AGENTS.md); record it in `WAVEFORM_ARCHITECTURE.md`'s
  companion stability section of the final report. No DSP or aux-routing
  gain changes without regression tests (assignment constraint).

### S11 — shutdown path audited  (verified sound; add a gate)
- Verified: `Main.cpp:19` destroys the window first;
  `AudioEngine engine` is the **first** data member of `MainComponent`
  (`Source/MainComponent.h:134`) → destroyed **last**, after all UI;
  `~AudioEngine` removes the device callback before anything else
  (`Source/AudioEngine.cpp:138-141`); `~Recorder` joins its writer
  (`Source/Recorder.h:21`); `~FxRack` resets editors
  (`Source/FxRack.cpp:9-12`); `MidiQueue`/peaks FIFOs are lock-free.
- Residual risks: repeated startup/shutdown cycles (device open/close,
  worker joins, SQLite close) have **no test**.
- **Fix:** CI startup/shutdown cycle — GUI smoke launches the app under
  Xvfb N times and quits cleanly (part of C2), plus S5/S6 worker-join
  coverage in unit tests. Note: `moreThanOneInstanceAllowed() == true`
  (`Source/Main.cpp:13`) means two instances share the SQLite index file —
  acceptable with SQLite's file locking; documented, not fixed here.

### S12 — explicitly *not* defects (do not "fix")
- Deck's staged-buffer pickup and stretcher reconfiguration on the audio
  thread (`Source/Deck.cpp:98-142`) — AGENTS.md rule 6 sanctioned bounded
  exceptions.
- `Eq3` coefficient rebuild from atomics on the audio thread
  (`Source/Mixer.cpp:90-103`) — AGENTS.md rule 7 pattern.
- Mixer chunked stack scratch (`Source/Mixer.cpp:173-267`) — RT-safe.
- `takePeaks` FIFO aggregation (`Source/AudioEngine.cpp:305-320`) — SPSC
  correct; channel peaks are written only by the audio thread into the FIFO.

---

## 2. Waveform findings (Milestone 2)

### W1 — single absolute-peak stream, no signed min/max  (H, functional — implemented in M2)
- **Implemented:** `lean::Waveform` stores per-level signed `{mins,maxs}`
  vectors over a mono mix-down (`0.5f * (L + R)`), built by
  `lean::buildWaveform` on the load worker and committed message-thread-side
  through the S5 swap; out-of-phase content cancelling to silence is pinned
  by the mixdown cases in `Tests/WaveformTest.cpp`. (Keeping stereo as two
  streams was not needed for the display spec — mono mix-down only.)
- **Where:** `Source/Waveform.h` — `peaks[i] = max(|L|,|R|)` over the window
  (comment `:12`), one value per bucket.
- **Fix:** new JUCE-free immutable model: per-level `{min,max}` pairs (mono
  mix-down first; optional stereo kept as two streams), built once off the
  audio thread, published via the S5 payload commit.

### W2 — no multiresolution pyramid / LOD  (H — implemented in M2)
- **Implemented:** level 0 targets 0.5 ms buckets, clamped to
  `[1024, 524288]` and never more buckets than frames; each further level
  ceil-halves down to a single bucket. `Waveform::aggregate(t0,t1,...)`
  selects the **coarsest** level whose bucket still fits the requested span
  (1-2 buckets per pixel -> O(viewport) paint) with a level-0 fallback when
  the capped grid is coarser than the span (very long tracks). The Fix
  text's "first level whose bucket ≤ pixel span" rule would pick level 0
  for every overview pixel and make the stripe O(full-track) per frame —
  the coarsest-fitting rule keeps the same O(viewport) bound the Fix
  requires. `MainComponent` paint issues one `aggregate` per pixel and does
  no analysis or allocation.
- **Where:** `Source/MainComponent.cpp:30-62` — paint maps time → one
  precomputed bucket (`:52-53`): narrow zooms repeat a single coarse peak
  (blocky, aliased); wide zooms point-sample (misses transients between
  buckets).
- **Fix:** LOD pyramid (level 0 = fine buckets ~4× viewport pixels, halving
  upward); paint aggregates min/max over each pixel's `[t0,t1]` range from
  the first level whose bucket ≤ pixel span. Zero work in `paint` beyond
  the O(viewport) aggregation over precomputed data; no analysis, no allocation.

### W3 — click-seek uses the wrong mapping  (H, user-visible — implemented in M2)
- **Implemented:** `paint` records `windowStart`/`windowSpan` for the frame
  it draws; `mouseDown` maps through `lean::seekTimeFromX` (shared with the
  tests, clamped at the viewport edges) and falls back to a whole-track
  seek before the first paint.
- **Where:** `Source/MainComponent.cpp:64-67` seeks with
  `event.x / getWidth()` (whole-track fraction) while paint shows the window
  `[start, start+span]` (`:42-48`, playhead anchored at 35%).
- **Fix:** the view records the exact window it rendered
  (`windowStart`, `windowSpan` written at the end of `paint`, message thread
  only) and `mouseDown` computes
  `t = windowStart + (x / viewportWidth) * windowSpan`, clamped to
  `[0, len]`. Identical formula in tests as in paint — single source of
  truth, no drift between what's drawn and where a click lands.

### W4 — zoom/wheel mutate state without repainting  (M — implemented in M2)
- **Implemented:** `zoomIn`/`zoomOut` (and therefore the wheel handler and
  the +/- buttons) call `repaint()` whenever the index actually changes;
  the legacy 30 Hz timer repaint stays until G10.
- **Where:** `Source/MainComponent.cpp:12-28` (`zoomIn`, `zoomOut`,
  `mouseWheelMove`).
- Today's only repaint is the 30 Hz whole-window timer
  (`Source/MainComponent.cpp:395`, `:773`) — masked, not correct.
- **Fix:** explicit `repaint()` on zoom changes (and targeted repaint once
  G10 reduces global repaints).

### W5 — no overview stripe  (M, spec-required — implemented in M2)
- **Implemented:** a 14 px overview stripe at the bottom of each waveform
  view (views >= 48 px tall) rendered from the pyramid, with a distinct
  whole-track click-seek (routed separately from the zoomed window seek),
  an accent playhead marker, and "Loading..." / "Analyzing..." text from
  `engine.deckState()` while no waveform payload is committed.
- Whole-track context bar with its own click-to-seek (distinct from zoomed
  seek), playhead marker, and load-progress indication (S5 states) rendered
  from the coarsest pyramid level — added with the W1/W2 model.

### W6 — build cost and integer safety of peak generation  (M — implemented in M2)
- **Implemented:** `int64`/`size_t` math end-to-end in
  `lean::buildWaveform`; explicit rejection above 24 h at the declared rate
  (corrupted headers) **before** any sample is read; bounded level-0 bucket
  count; non-finite samples excluded from the payload (never propagated,
  per-bucket fixup to silence); `AudioEngine` no longer derives a target
  bucket count.
- **Validation (implemented):** full synthetic suite in
  `Tests/WaveformTest.cpp` — impulse in/out of its bucket, full-scale and
  out-of-phase mix-down, stereo cancellation, sine amplitude bounds,
  10-frame/1-frame/zero-length inputs, >24 h frame-count rejection, odd
  (999-bucket) pyramid boundary with pairwise-downsample exactness and
  global-range propagation, LOD level selection, W3 viewport-edge seeks,
  aggregate zero-width/out-of-range/reversed windows, non-finite
  exclusion, payload duration/rate/frame consistency, and the W8 zoom
  table.
- **Residual:** frame counts above `INT_MAX` are now handled correctly via
  `int64` but not exercised against a real (multi-GB) allocation in CI;
  the decode path itself is `int`-bounded today (`Source/AudioEngine.cpp`
  `runLoadJob`).
- **Where:** `Source/Waveform.h:25` casts `numFrames / rate / n` and frame
  counts to `int`; `Source/AudioEngine.cpp:287` derives the target bucket
  count (256..8192). O(track) single pass — fine off-thread (S5), but
  frame counts > `INT_MAX` (≈12.4 h at 48 kHz, or corrupted headers) wrap.
- **Fix:** saturating/guarded types (`int64`/`size_t` end-to-end), reject
  absurd durations explicitly, and test with synthetic huge frame counts.
- **Validation (planned at review time):** spec's synthetic suite in
  `Tests/WaveformTest.cpp` (baseline at review: tone/silence/empty only;
  now implemented — see **Validation (implemented)** above):
  impulse, ±full-scale peaks, stereo channel difference, sine amplitude
  bounds, short and zero-length inputs, huge frame counts, pyramid
  downsample boundaries, LOD level selection, viewport edge seeks (W3
  formula at x=0 and x=width), non-finite input rejection, and
  duration/rate consistency between payload and deck.

### W7 — playhead source  (verified; keep — unchanged in M2)
- Paint reads the engine's authoritative position
  (`Source/MainComponent.cpp:43`); after S5 the payload swap must never
  tear the playhead read (message-thread-owned state — no change needed).
- **Post-implementation note:** the waveform payload still commits only in
  `AudioEngine::handleAsyncUpdate()` on the message thread, so the playhead
  read in paint remains single-threaded as required. Deck-side
  length/duration consistency is covered by `deckload_test`.

### W8 — no zoom-state persistence or tests for the zoom table  (L — tests implemented in M2)
- **Implemented:** the table and its helpers moved to the JUCE-free layer
  (`lean::kZoomSeconds`, `lean::clampZoomIndex`, `lean::zoomSpanSeconds`,
  shared by the view and the tests); `Tests/WaveformTest.cpp` pins
  both-end index clamping, "whole" -> full duration, span >= length ->
  length, and invalid length -> 0. Cross-session zoom persistence was not
  part of the fix text (tests only) and remains out of scope.
- Zoom table `{4,8,16,32,64,128,256,whole}` already matches the assignment
  (`Source/MainComponent.h:39-41`, default 32 s); add tests that the index
  clamps at both ends and that "whole" (`0.0`) maps to full duration.

---

## 3. GUI / layout / design findings (Milestones 3-6)

### G1 — outdated numeral emblem  (implemented in M3)
- `Source/TopBar.cpp:106-124` draws the neon-ring **"6"** badge
  ("mix-cover style"); `Source/TopBar.cpp:130` reserves 56 px for it.
  Remove the emblem, reflow `TopBar::resized`, keep the wordmark.
- **Fix (implemented):** the RIG brand badge replaces the numeral in the
  same 56 px slot — first-party `Source/Assets/rig_mark.svg` (padded
  viewBox so the disc never clips, hairline rim, palette-tuned disc
  `#1d2229` / ink `#e8ecf1`), embedded through the existing `TRANSITAssets`
  binary-data target and drawn with `juce::Drawable::createFromImageData`
  at the historic optical centre (34, 32); wordmark kept; `TopBar.h`
  VOIDX-style comment removed. Provided masters in
  `Media/rig-wordmark/` were modernized the same way (padding + rim).
  CI: run 39254042952 green (8/8) at `001a82e`; badge verified from the
  `gui-smoke-shots` artifact (ink centred within 0.5 px of the badge box).

### G2 — neon "VOIDX-style" language conflicts with the design rules  (implemented in M3)
- `Source/TopBar.h:11` comment ("VOIDX-style"), `DJLook::neonCyan/neonPink`
  + `drawNeonEdge` (`Source/DJLook.h:18-23`, used at `TopBar.cpp:104`,
  `MasterStrip` gradient `TopBar.cpp:25-26`), FileList "blue glow pill"
  (`Source/MainComponent.cpp:82-91`).
- **Fix:** replace with the dark-glass token system — cyan/ice-blue for
  information, amber for transport, red for record/clip/danger; thin lit
  borders instead of neon gradients; no gratuitous glow (assignment design
  rules). Semantics kept where they already fit (cyan waveforms, amber
  playhead, red record) so re-skinning doesn't break recognizability.
- **Fix (implemented):** `drawNeonEdge` → `drawLitBorder` (one ink
  hairline at 20 % alpha) behind the top bar and the `GlassPanel` browser
  backdrop; MasterStrip position-gradient → information cyan with alarm
  red above 0.98 (near clip); active tab + selected-row glow pills → flat
  `cyan`/`alarm` fills with dark on-fill text (matching the existing amber
  "To A" language); off-palette `tabBlue`/`neonCyan`/`neonPink` tokens
  deleted. CI: run 39258511344 green (8/8) at `6b8a808`, look verified
  from `gui-smoke-shots`.

### G3 — hardcoded, non-responsive layout  (M4 partial implemented; final in M6)
- `centerW = 300` and friends (`Source/MainComponent.cpp:835-931`),
  fixed browser/meter/top-bar sizes; window forced non-resizable +
  fullscreen (`Source/Main.cpp:34-37`).
- **Fix:** constraint-based layout with per-region minimum sizes, verified
  at 1100×824, 1280×800, 1440×900, 1920×1080 (CI screenshots, C2) and
  high-DPI. Reevaluate unconditional fullscreen (allow windowed +
  resizable); **keep** F11 toggle and Escape-exit
  (`Source/Main.cpp:44-53`) and the existing WM minimize/close behaviour
  (`Source/TopBar.cpp:79-86`).
- **Fix (M4 partial, implemented):** the window is resizable with a
  `setResizeLimits` floor of 1100×824 (the smallest CI viewport) and a
  16384 ceiling (`Source/Main.cpp`); it still opens fullscreen and the
  F11/Escape behaviour is unchanged, so windowed mode is now reachable
  without new defaults. Layout responsiveness across sizes is exercised
  every run — the harness resizes `MainComponent` through all four
  viewports directly, which is the same `resized()` path a user drag
  takes. Still M6: per-region minimum hardening beyond the window floor
  (`centerW=300`), default-windowed evaluation, high-DPI sweep.

### G4 — linear (non-dB) meters  (implemented in M4)
- `ChannelMeter` scales amplitude linearly (`Source/MainComponent.h:49-66`);
  master meter likewise (`Source/MainComponent.cpp:784-792`) fed by linear
  peak values from `takePeaks`.
- **Fix:** log/dB scale with peak-hold, amber/red thresholds per the color
  rules; keep the FIFO source (S12).
- **Fix (implemented in M4):** all three level displays render in a
  dB-mapped space — `DJLook::meterUnit()` maps a linear peak to 0..1 over
  −60..0 dB (`20·log10`, clamped; NaN/negative input floors to 0). Bars
  attack instantly and fall at a constant ~18 dB/s (`kMeterFall`) instead
  of the old `·0.94` linear decay. The channel VUs and the master meter
  share `MeterHold` (instant attack, ~0.75 s hold at the 30 Hz tick, then
  ~14 dB/s fall) drawn as an ink hairline; the TopBar master history strip
  uses the same mapping, with its clip colour still on the raw peak
  (alarm above 0.98 ≈ 0 dB). Colour rules unchanged: cyan → amber → red
  ramps (amber stops at 0.6/0.7 of the span). FIFO source untouched (S12).
  CI has no audio device, so `MainComponent::harnessSetMeters` seeds known
  levels (0.72 / 0.55 / 0.88) before the pass-2 snapshots — real
  decay/hold behaviour still needs human verification with an audio stream.

### G5 — status label doubles as filename/error/state display  (implemented in M4)
- `Source/MainComponent.cpp:224,461-464,657-662`.
- **Fix:** dedicated transport/status row per deck with the S5 state machine
  (Empty / Loading / Analyzing / Ready / Error), error text styled in alarm
  red, loading state visualized (also feeds W5 progress).
- **Fix (implemented in M4):** the row splits into a bold 11 px state chip
  (left, 86 px) and a payload label (right): `EMPTY` dimInk /
  `LOADING..` cyan with a 3 Hz animated dot ellipsis / `ANALYZING` cyan /
  `READY` amber with the filename in ink / `ERROR` alarm with the engine
  error text also in alarm. Text and colour update only when changed, so
  the 30 Hz tick stays cheap. The harness gained a deterministic `error`
  pass (nonexistent path on deck A, poll for `Error`, snapshot ×4
  viewports — deck B stays `EMPTY` in the same frame), so
  Empty/Ready/Error chips are verified in PNGs (28 shots now);
  `Loading..`/`Analyzing` are too transient to capture deterministically —
  like G4's live decay/hold, that stays a human-verification residual.

### G6 — browser rows show filename only  (M5)
- `Source/MainComponent.cpp:95-97`.
- **Fix:** metadata columns (BPM from `getBpm`, duration `formatTime`,
  folder, added-date if available), keyboard navigation, clear selection
  state — consistent with the design tokens; search debounce already S6.

### G7 — scattered color/style literals  (M3 colour roles implemented; spacing/radii/type roles go to GUI_DESIGN_SYSTEM.md)
- Raw colours at `MainComponent.cpp:31`, `MainComponent.h:54`,
  `TopBar.cpp:16,40,117`, `DJLook.cpp` gradients — centralize in `DJLook`
  token tables (colour roles, spacing, radii, borders, type roles via the
  existing embedded Adriatica Next typefaces — `DJLook::getTypefaceForFont`,
  `Source/DJLook.h:31`, keep).
- **Fix (implemented, colour roles):** new surface tokens `DJLook::deep`
  (deck canvas, master strip, output-meter well) and `DJLook::well`
  (overview stripe, channel VU); raw literals centralized in `TopBar`
  (strip background, tab off-text → `dimInk`) and `MainComponent` (deck
  canvas, overview, meter, channel VU); `TextButton::textColourOnId`
  uses the `bg` token. Off-palette `tabBlue`/`neonCyan`/`neonPink`
  deleted with G2. Spacing, radii and type roles are deliberately *not*
  half-invented here — they are a deliverable of
  `GUI_DESIGN_SYSTEM.md`.

### G8 — global 30 Hz whole-window repaint  (M3 start implemented; keeps growing with the redesign)
- `startTimerHz(30)` + full `repaint()` (`Source/MainComponent.cpp:395`,
  `:773`).
- **Fix:** keep one timer, but repaint only what changes (waveforms, meters,
  clock, transport readouts, top-bar strip); waveform playhead updates can
  ride the same tick with per-component repaint. This is the main paint-cost
  risk once the layout becomes richer.
- **Fix (start, implemented):** the tick no longer calls a blanket
  `repaint()` — it invalidates the two deck waveforms (playheads ride the
  tick; this also keeps seek-while-paused visible, since no other call site
  repaints the waveform) plus the 26 px output-meter strip. Everything else
  updated in the callback already invalidates itself (labels, sliders,
  tabs, channel VUs, master strip). Residual, documented: an idle *loaded*
  waveform still re-renders every tick — gating that on position/state
  change detection is deferred to the M5/M6 paint pass.

### G9 — parked tabs  (keep as-is)
- FX/SAMPLER/LOOP disabled with "Not in this build" tooltips
  (`Source/TopBar.cpp:46-52`); REC active. No new DJ subsystems in this
  assignment — restyle only, keep disabled semantics and tooltips.

---

## 4. CI / test infrastructure (introduced progressively)

### C1 — branch pushes don't trigger CI  (fix immediately, Milestone 1)
- `.github/workflows/linux.yml:3-5` triggers on `push: main` and
  `pull_request: main` only. **Open a draft PR** from
  `feat/gui-waveform-stability` right after the first substantive push so
  every subsequent commit gets the full matrix (PR events run on the head
  SHA). Never merge (assignment constraint).

### C2 — no GUI smoke / screenshot job  (implemented in M3)
- Current `UiShot` admits headless painting is unreliable
  (`Tools/UiShot.cpp:7-9`); workflow installs no X server
  (`.github/workflows/linux.yml:66-75`).
- **Plan:** new `gui-smoke` job: install `xvfb`, run a real windowed peer
  under `xvfb-run` with a serviced JUCE message loop, capture deterministic
  preview states (empty decks; two synthetic in-repo test tracks generated
  at test time — never shipped as demo data; playing/paused; short zoom +
  whole-track; mixer/FX page states) at the four viewports, upload PNG
  artifacts named by view×viewport, and **fail** if any expected artifact
  is missing, undecodable, wrong-sized, all-black/transparent, out of
  bounds, or shows control intersections/escapes. Documented in
  `CI_VISUAL_TESTING.md`. Aesthetics are reviewed by a human from the
  artifacts — PNG existence alone never proves design correctness.
- **Fix (implemented):** `Tools/GuiSmoke.cpp` (SmokeWindow + serviced
  `runDispatchLoopUntil` pumps, manual RIFF tone writer, 3× S11
  create/destroy cycles, 6 states × 4 viewports, per-frame re-decode +
  size + bright-fraction validation) and the `gui-smoke` workflow job
  (needs: guard; artefacts path `build/GuiSmoke_artefacts/Release/GuiSmoke`).
  Three CI corrections were needed (≤5 budget): (1) JUCE console binaries
  land under `*_artefacts/<config>/`; (2) JUCE interns WM/ICCCM/EWMH atoms
  with `only_if_exists` and then writes `WM_PROTOCOLS` unchecked — bare
  Xvfb has none → fatal BadAtom (serial 81); seeded 30 atom names server-
  globally before JUCE init; (3) `JUCEApplicationBase::isStandaloneApp()` is
  false for console apps, so JUCE never installs its error-swallowing X
  handler and Xlib's default handler aborts the process — the harness now
  installs its own swallow handler with capped diagnostics/backtraces
  (mirroring the real app, which silently swallows the same residual
  errors) plus `seedX11Atoms` reporting. Validation: run 39183275899
  green (8/8), 24/24 shots validated, artifact `gui-smoke-shots`.
  Phase 2 (intersection/escape checks, golden diffs) remains explicitly
  deferred in `CI_VISUAL_TESTING.md`.

### C3 — no sanitizer jobs  (implemented on this branch)
- Done: a bounded ASan+UBSan job over the JUCE-free test targets
  (clang Debug, `-fno-sanitize-recover=all`, LeakSanitizer on, failure logs
  uploaded) plus a TSan job over the concurrency tests (MidiQueue,
  LoadSchedule, Library, Mixer). The existing 4-config matrix is untouched
  (it is the warning gate); no sanitizer job replaced a required gate.

### C4 — test gaps to close with Milestone 1
- S3 `MidiQueue` timing/ordering/overflow tests ✓; S5 load-lifecycle +
  cancellation/newest-wins ✓ (`loadschedule_test`); S2/S1 FX
  suspend-ack + retire-deferral protocol tests ✓ (`fxrack_test` — in-process
  stub plugin, friend seam in `FxRack.h`); S6 concurrent Library access ✓
  (`library_test`); S4 paused-load length ✓ (`deckload_test`); S11
  startup/shutdown cycles via GUI smoke — deferred to C2/Milestone 3 (Xvfb
  harness). No test may be disabled or weakened to go green (assignment
  hard rule).

### C5 — gates preserved
- Guard job (identity strings + SPDX), GCC/Clang × Debug/Release, all
  CTest, strict first-party flags (`CMakeLists.txt:133-173`), pinned
  FetchContent deps, `permissions: contents: read`. New source files must be
  added to `TRANSIT_FIRSTPARTY_SOURCES` (`CMakeLists.txt:116-131`) **and**
  each target's `target_sources` (app, UiShot/smoke, tests) to stay under
  `-Werror`.

---

## 5. Milestone mapping

| Milestone | Findings | Key CI evidence |
|---|---|---|
| **M1 — Stability foundation** | S1-S9, S11 gate, C1, C3, C4; AGENTS.md authorization note | 4-config matrix + new tests + sanitizer job green |
| **M2 — Waveform 2.0** | W1-W8 | extended `WaveformTest` green in matrix |
| **M3 — Dark-glass design system** | G1, G2, G7, G8(start), C2 introduced | GUI smoke + PNG artifacts at 4 viewports |
| **M4 — Deck redesign** | G3(partial), G4, G5, W3/W5 integration | screenshots reviewed per iteration |
| **M5 — Mixer + browser** | G3(partial), G6, S6 UX follow-through | screenshots + library tests |
| **M6 — Polish & validation** | G3(final), G8(finish), viewport/high-DPI sweep, docs (`WAVEFORM_ARCHITECTURE.md`, `GUI_DESIGN_SYSTEM.md`, `CI_VISUAL_TESTING.md`, `DEVELOPMENT_REPORT.md`) | full-matrix final run, artifacts archived, PR left open |

Out of scope by rule: loops/samplers/hotcues features, dependency major
upgrades, merges to `main`, releases.

---

## 6. Honest status

- This document is **static review only** — no local compile/run was
  performed (assignment forbids it). All findings are grounded in source at
  `46dd9ac` with file/line anchors as cited.
- "Verified sound" items (§S12, W7, S11) are reviewed, not executed;
  execution evidence arrives only with CI runs on this branch.
- Deferred: aesthetic judgement of the redesign (human review of CI PNG
  artifacts), third-party plugin behaviour beyond lifecycle correctness, and
  any residual concurrency risks listed in the final `DEVELOPMENT_REPORT.md`.
