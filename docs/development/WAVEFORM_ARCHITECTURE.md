# TRANSIT — Waveform architecture

What ships as of milestone M2 ("Waveform 2.0"), plus the repaint-policy
finishes from M3/M6. This documents the design that is in the tree — not
aspirations. Findings W1–W8 and their status live in
`GUI_AND_STABILITY_REVIEW.md` §2; the executable evidence is
`Tests/WaveformTest.cpp` and the CI `gui-smoke` frames.

## 1. Goals (from the review)

- **Signed min/max** per bucket instead of a single absolute-peak stream
  (W1), so drawn envelopes show phase, not just magnitude.
- **A multiresolution pyramid** so any window paints in O(pixels) instead
  of O(track) (W2).
- **Click-seek through the same mapping paint uses** (W3) — a click lands
  exactly where the window is drawn.
- **Explicit repaints** for zoom and seek (W4, G8) instead of a blanket
  30 Hz full-window repaint.
- **Whole-track overview stripe** (W5) driven by the same pyramid.
- **Bounded, integer-safe build cost** (W6): `int64` math, an explicit
  >24 h rejection before any sample is read, bounded bucket counts.
- Playhead read from the engine's authoritative position on the message
  thread (W7, verified — unchanged).

## 2. Data model — `lean::Waveform` (`Source/Waveform.h`, JUCE-free)

```text
struct Waveform {
    struct Level { std::vector<float> mins, maxs; };
    std::vector<Level> levels;   // [0] finest ... back() coarsest (one bucket)
    double sampleRate, durationSeconds;
    std::int64_t frames;
};
```

- **Level 0 build** (`buildWaveform`): mono mix-down `0.5*(L+R)` per frame
  (W1), target bucket size 0.5 ms (`seconds * 2000`), clamped to
  `[1024, 524288]` buckets and never more buckets than frames. Frame →
  bucket mapping is integer (`f * n / frames`), so it is exact and
  overflow-checked by the same `int64` bounds. Non-finite samples are
  skipped, never propagated; buckets that saw only non-finite input are
  fixed up to silence (`min = max = 0`).
- **Pyramid**: successive levels are ceil-halved pair-wise
  (`n1 = (n0 + 1) / 2`, the odd trailing bucket passes through) until a
  single bucket remains. Every level therefore holds the exact min/max of
  its children — no recomputation from samples at paint time.
- **Guards**: null payload, non-positive frames/rate, non-finite duration,
  and >86400 s at the declared rate (corrupted headers) all return an
  invalid, empty waveform before any sample is touched (W6).
- **Validity**: `valid()` = at least one level, matching min/max sizes,
  positive duration. `reset()` clears everything; the engine replaces the
  payload only on the message thread (W7).

## 3. LOD query — `aggregate(t0, t1, outMin, outMax)`

Single source of truth for paint, the overview stripe, and the tests:

- chooses the **coarsest level whose buckets still fit the requested time
  span** (bucket seconds ≤ span, with a ±1e-9 double tolerance so grid
  rounding cannot flip the level);
- sums per-bucket min/max over `[t0, t1]` clamped to the track;
- a **zero-width range resolves to its containing bucket** (documented
  edge case, tested);
- costs O(pixels) per painted column regardless of track length.

## 4. Zoom and seek (`Source/Waveform.h`, `WaveformView`)

- `kZoomSeconds[8] = {4, 8, 16, 32, 64, 128, 256, 0}` — last entry means
  whole track; default index 3 (32 s). `clampZoomIndex` clamps both ends;
  `zoomSpanSeconds(idx, trackLen)` resolves whole/oversized entries to the
  full duration (W8: clamping and mapping are unit-tested; cross-session
  zoom persistence is explicitly out of scope and stays unimplemented).
- The view caches `windowStart`/`windowSpan` **at paint time**
  (`windowSpan == 0` until first paint → whole-track fallback);
  `seekTimeFromX(windowStart, windowSpan, x, viewportWidth)` maps a click
  through exactly those numbers (W3), clamping the viewport fraction to
  `[0, 1]`; callers clamp to `[0, length]`.
- Zoom changes (`zoomIn`/`zoomOut`/`setZoomIndex`) repaint explicitly (W4).
  Key-bound zoom (`+`/`-`) goes through the same functions.

## 5. Overview stripe (W5)

`paintOverview` renders the bottom `kStripeHeight = 14` px (only when the
view is ≥48 px tall) from the same pyramid: per-column `aggregate`, played
region tinted behind the playhead, a 2 px accent playhead marker, and the
S5 load-state text while no waveform is committed yet (Empty → nothing,
Loading/Analyzing/Error → state message).

## 6. Repaint policy (G8, finished in M6)

One 30 Hz timer drives the UI; the blanket `repaint()` is gone:

- **Deck waveforms** repaint only when a value they paint changed: load
  state or a quantized playhead-position token (1 ns integer truncation —
  a change token, not a measurement). This keeps seek-while-paused visible
  (position changes) while an idle loaded waveform stops re-rendering
  every tick. Zoom repaints itself (W4).
- **Output-meter well** (bottom 26 px) repaints every tick — meters decay
  continuously while audio runs.
- Everything else in the tick (labels, sliders, tabs, channel VUs,
  top-bar master strip) invalidates itself when its text/value changes.

`GuiSmoke` snapshots bypass the repaint machinery entirely
(`createComponentSnapshot` renders current state), so frame content is
independent of this gating.

## 7. Pipeline and threads

```text
loadFile (message)  → LoadJob queue (newest-wins gate)
                    → S5 worker: decode → analyze (BPM) → buildWaveform
                    → handleAsyncUpdate (message thread): waves[d] committed
paint/timer/seek     → message thread only (no locks, no audio-thread state)
audio thread         → never touches the waveform payload (JUCE-free engine)
```

Library indexing (SQLite) is a separate worker path (S6); it never feeds
the paint path directly.

## 8. Validation

`Tests/WaveformTest.cpp` (~457 lines, run in every matrix config):
pyramid shape for tone/short/odd(999-bucket)/one-bucket cases; sine
amplitude bounds and mix-down scaling (full-scale, half-scale, stereo
cancellation); impulse bucket placement and bucket-seconds math; LOD level
selection (coarser chosen / finest fallback); zero-length, 1-frame,
10-frame inputs; >24 h rejection (payload stays empty); non-finite
exclusion with payload still valid; reversed/zero-width/out-of-range
aggregate windows; the full zoom table (4 s entry, whole → length,
span ≥ length → length, both-end clamping, invalid length → 0); and W3
seeks at x = 0, x = width, midpoint, and both clamps.

CI additionally renders the pyramid in `gui-smoke` frames at four
viewports (`empty/loaded/zoom4s/whole/playing/fx/error/hidpi`), and the
release-artifact build proves the whole path link-identically.

## 9. Known limits (honest)

- Zoom index is per-session only (reset to 32 s at launch) — persistence
  was out of scope for W8 and remains so.
- Frame counts above `INT_MAX` are handled in `int64` inside
  `buildWaveform` but not exercised against a real multi-GB allocation in
  CI; the decode path itself is still `int`-bounded (W6 residual).
- >24 h inputs are rejected outright (design decision for corrupted
  headers, not a truncation bug).
- Analysis (BPM, waveform) happens once per load on the S5 worker; there
  is no progressive/streaming waveform — the overview stripe shows the
  load state until commit (documented S5 behaviour).
