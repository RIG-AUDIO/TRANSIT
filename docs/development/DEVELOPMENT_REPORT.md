# TRANSIT — Development report (GUI / waveform / stability assignment)

Final report for the assignment brief of 2026-10-18: dark-glass GUI
redesign, waveform 2.0, and stability fixes, built and validated
**exclusively through GitHub Actions** — no local builds were run at any
point. Branch `feat/gui-waveform-stability`, draft PR
[#1](https://github.com/RIG-AUDIO/TRANSIT/pull/1) (never merged), baseline
`main` at `46dd9ac`.

## 1. Method and discipline

- Every change: commit → push through the identity-guarded
  `scripts/transit-git.sh` → CI on PR #1 → fix loop. Red runs stayed
  within the ≤5-correction budget per item.
- Only non-compiled local operations were used: file inspection/editing,
  `bash -n` script syntax checks, guard smoke runs, git/gh CLI.
- Identity guard (`scripts/verify-github-identity.sh`) and the
  forbidden-identity/SPDX scan ran green in every single run; the
  pre-push hook scanned every push.

## 2. Milestone ledger (every push = one full CI run)

| Step | Commit | Run | Result |
|---|---|---|---|
| M1 first fixes (S1–S9, S11, C1, C3, C4) | `a57ec1a` | 39169439600 | success |
| M2 waveform 2.0 (pyramid/zoom/seek, W-tests) | `343785b` | 39175047598 | success (2 corrections) |
| M3 GUI smoke harness (C2) | `eee3695` | 39183275899 | success (3 corrections) |
| M3 build badge (G1) | `001a82e` | 39254042952 | success |
| M3 design tokens (G2/G7) | `6b8a808` | 39258511344 | success |
| M3 wave repaint start (G8) | `9541e71` | 39260797635 | success |
| M3 docs + release artifact upload | `ac0a11f` | 39261788109 | success |
| M4 dB meters + peak hold + meter seam (G4/G4f) | `6864188` | 39293792851 | success |
| M4 status chip row (G5) | `8c7555a` | 39298494214 | success |
| M4 resizable window, 1100×824 floor (G3) | `e19f6b1` | 39301821885 | success |
| M5 browser metadata columns + Enter-to-load (G6) | `da260f9` | 39304827365 | success |
| M6 wave repaint gating finish (G8) | `d8b5e64` | 39307525300 | success |
| M6 hiDPI 2× snapshot sweep + G3 close-out | `5f388e8` | 39309767677 | success |
| M6 architecture/design docs | `dbd067d` | 39312202736 | success |
| M6 SonarQube fixes 1/2 | `3ab8972` | 39316083483 | success |
| M6 SonarQube fixes 2/2 | `de85052` | 39318471828 | success |
| M6 report (this file) | see PR #1 checks | see PR #1 checks | see §7 |
| Promotion to `main` (merge commit) | `02bdc10` | 39323566806 | success |
| Round 2 — metadata columns + zoom state | see PR #2 | see PR #2 | see §11 |
| Round 3 — browser scroll + per-file zoom memory (W9) | see PR #2 | see PR #2 | see §12 |
| Round 4 — mixer visual upgrade + bipolar EQ fill fix | see PR #2 | see PR #2 | see §13 |
| Round 5 — Pioneer DDJ-FLX4 factory MIDI map | see PR #2 | see PR #2 | see §14 |

Red-run history (all before the current streak): 9 red runs total —
`ff769bd`/39129895367, `6e7c362`/39161205557, `fb9ec0d`/39166640510,
`9634f94`/39168411268 (M1-era), `4b7d303`/39172597945 and
`2065746`/39173055820 (M2), `42ab68c`/39178045136,
`3107fb3`/39179044170, `7e08e08`/39180984428 (M3 C2). Every item stayed
within budget; since `7e08e08` the branch has run **14 consecutive green
runs** through `de85052`.

## 3. What each CI run proves (8 jobs)

| Job | Validates |
|---|---|
| Forbidden-identity scan | No legacy identity strings (carve-outs applied); SPDX `GPL-3.0-only` headers on all first-party sources |
| gcc Debug / gcc Release | Primary toolchain, strict flag set with `-Werror`, full build + `ctest` |
| clang Debug / clang Release | Second toolchain, same gates |
| ASan+UBSan (JUCE-free tests) | Memory safety and UB on the test suite (`ctest -E fxrack_test`) |
| TSan (queue/lifecycle tests) | Thread-sanitizer on queue and lifecycle tests |
| GUI smoke (Xvfb screenshots) | Launches the real release app headless; captures and validates 29 frames |

Artifacts per run: `transit-linux-x64-release` (gcc Release app),
`gui-smoke-shots` (29 PNGs), plus failure-only diagnostic logs.

## 4. Visual evidence

29 screenshots per run: 7 states (`empty`, `error`, `loaded`, `zoom4s`,
`whole`, `playing`, `fx`) × 4 viewports (1100×824, 1280×800, 1440×900,
1920×1080) plus `hidpi-2560x1600.png` (logical 1280×800 at 2× scale).
Frame content is pixel-validated (dims + bright-fraction) in-harness;
aesthetic judgement of the frames remains a human step (phase-2 list in
`CI_VISUAL_TESTING.md`).

## 5. Findings status (summary; per-finding detail in
`GUI_AND_STABILITY_REVIEW.md`)

- **S1–S9, S11, C1, C3, C4** — fixed by M1. **S10** (aux bypass tail-cut)
  documented as intended behaviour. **S12** — not defects.
- **W1–W6, W8** — implemented and gated by WaveformTest (~42 named
  checks) in M2. **W7** — verified keep. **W8 persistence** was out of
  scope for the original milestones: round 2 covers the per-deck zoom
  index (`Source/UiState.h` + `Tests/UiStateTest.cpp`, see §11) and
  round 3 the W9 remainder — browser scroll + per-file zoom memory
  (§12).
- **G1** (badge) M3 · **G2/G7** (tokens/colours) M3 · **G3** window floor
  M4 + coverage M6 (default-windowed vs fullscreen = human decision,
  fullscreen kept) · **G4** meters M4 · **G5** status row M4 · **G6**
  browser M5 · **G8** repaint gating M3+M6 · **G9** tabs kept as-is ·
  **G10** zoom tests M2.
- **C2** test-harness seams M3 · **C5** guard/CI gates preserved.

## 6. SonarQube findings — verification and fixes

All five open-issue classes from the SonarQube snapshot were verified
against this branch (note: the snapshot's line numbers partially map to
`main`, but every class exists here too), then fixed:

| Class | Verdict | Fix |
|---|---|---|
| Tests use public temp dirs (CWE-377, High) ×3 | True positive | `Tests/TempDir.h`: per-process unique dir, exclusive creation, owner-only perms; applied to Decode/Library/Recorder tests **and** FxScanTest (same pattern, not yet scanned) |
| Atomic orderings ≠ `seq_cst` (High) ×183 in 16 files | True positive as a rule; code was correct acquire/release design | Mechanical upgrade of all 183 sites to `std::memory_order_seq_cst` (strictly stronger); ASan/UBSan/TSan re-validated in run 39318471828 |
| `strlen` safety (Medium) ×3 | Practically false positive (literals only) | `Decode.cpp`/`Library.cpp` suffix checks rewritten with `std::string_view`; zero `strlen` calls remain |
| `Deck.cpp` lock balance (L109) | False positive as a bug; style-level | `adoptStaged()` converted to `std::unique_lock(try_to_lock)` — RAII, still realtime-safe |
| `scripts/` use `[` not `[[` (High) ×~20 | True positive (style) | All 4 flagged scripts converted (shebangs are `bash`); `.githooks/pre-push` converted too (same pattern) |

Shipped as two commits: `3ab8972` (run 39316083483) and `de85052`
(run 39318471828), both green. A SonarQube re-analysis is needed to
confirm the dashboard shows them closed — that runs outside this repo's
CI.

## 7. Validation status of this report

This file's own commit is validated by the then-latest workflow run on
`feat/gui-waveform-stability`; see the checks on PR #1
(<https://github.com/RIG-AUDIO/TRANSIT/pull/1>). At delivery time the
latest run was green, and its run ID is cited in the delivery message.

## 8. Residual risks and known limits (honest list)

- **Human verification still required:** G4 meter decay/hold in live
  audio, G5 `LOADING`/`ANALYZING` animated states (needs a slow disk),
  aesthetic sign-off on the 29 frames, and the phase-2 manual checklist
  in `CI_VISUAL_TESTING.md` (audio path with a real device, MIDI, FxRack
  with third-party plugins).
- **G6 BPM/duration columns** — implemented in round 2 (index columns
  `bpm` / `duration_ms`, scan-time analysis, browser columns); see §11.
- **G3 default window** stays fullscreen; making windowed the default is
  a product decision, not taken here.
- **S10** tail-cut on aux bypass/empty is intended and documented.
- **SQLite** Library keeps a single serialized connection (mutex), not a
  connection-per-thread design.
- **W6 residual:** >INT_MAX-frame files are rejected by policy, not
  exercised against a real multi-GB allocation.
- **gui-smoke** prints expected X11 warnings (documented); snapshot
  content is validated by the harness assertions, not by an image
  comparator with golden files.
- **Wave zoom (W8)** persists across sessions as of round 2 (§11), and
  round 3 adds the W9 remainder (§12): the browser scroll position and
  per-file zoom memory. Not covered: per-file *scroll* memory (the wave
  window is playhead-following, so there is no independent scroll to
  store) and zoom entries for files outside the 64-entry cap.
- The app binary a human runs locally may be an older artifact; always
  re-download `transit-linux-x64-release` from the latest green run.
- The promoted `main` branch and this round-2 branch are **not**
  production-ready: they are CI-validated milestone states pending human
  review.

## 9. Deliverables

- Code: GUI redesign (tokens, meters, status row, browser columns,
  resizable window), waveform 2.0 (pyramid LOD, zoom, seek, repaint
  gating), stability fixes per §5, SonarQube fixes per §6.
- Tests/harness: `Tools/GuiSmoke.cpp` (29-frame matrix incl. hiDPI),
  `Tests/TempDir.h`, seams `harnessSetMeters`/`harnessSetBrowserPath`,
  WaveformTest zoom/seek gates.
- Docs: `GUI_AND_STABILITY_REVIEW.md`, `CI_VISUAL_TESTING.md`,
  `WAVEFORM_ARCHITECTURE.md`, `GUI_DESIGN_SYSTEM.md`, this report.
- CI: 8-job matrix, release-app artifact upload, screenshot artifacts,
  identity+SPDX guard — all green on the commits above.

## 10. Declarations

No local compilation or app launches were used for validation; no test
was disabled or weakened; guard and CI checks were never touched except
for the style conversion in §6 (behaviour verified in CI itself); the
README warning was preserved; the legacy identity strings remain absent
from the tree (scan green in every run).

PR #1 was merged into `main` on 2026-10-19 (merge commit `02bdc10`) on
explicit human instruction, which superseded the "PR #1 stays draft"
rule of the original brief (AGENTS.md rule 11 scope). The merge was
verified by the `push: main` CI run.

## 11. Round 2 — library metadata columns and zoom persistence

Scope (chosen with the human after the merge): the two residuals that the
original brief had deferred, on a new branch
`feat/metadata-columns-zoom-state` (PR #2, draft).

- **G6 — BPM and duration columns.** The library index gains two
  columns (`bpm`, `duration_ms`), added in place by a migration so index
  files written earlier keep working. A scan analyses new or changed
  files on the worker thread (decode + `lean::detectBpm`); files above
  64 MB stay unanalysed, and unreadable files keep unknown values
  instead of failing the scan. The browser reads the values through
  `Library::metadataFor` and falls back to a header-only duration probe
  for files outside the index; an unknown tempo shows `—`. The new
  columns drop out on narrow lists (time < 640 px, bpm < 760 px).
  Gate: `library_test` writes a real 120 BPM click track and asserts the
  recovered tempo (~120) and duration (~5 s).
- **W8 — zoom persistence.** The per-deck zoom index is stored in the
  settings file (`zoom0=`, `zoom1=`) through the JUCE-free
  `Source/UiState.h` helpers, so defaults, clamping and malformed input
  are unit-tested (`uistate_test`) instead of hiding inside the UI. Any
  zoom change writes the settings file; startup restores the index.
- Known limits of this round: the tempo estimate is the existing
  autocorrelation detector (sine tones and silence have no tempo, so
  those cells stay `—`); the duration probe covers formats JUCE can read
  headers for; no scroll offset or per-file zoom memory is stored yet.
  The scan analyses outside the library lock (a long folder never blocks
  the UI), and a file that could not be read at scan time keeps its
  unknown values until its size or timestamp changes.

**CI history (GitHub Actions, no local builds).** Two red runs, each
fixed once by inspection of the failing job log:

1. `39325970646` — `probeDuration` called a non-const
   `AudioFormatManager::createReaderFor` through a const member; fixed by
   making the method non-const (`838f60c`).
2. `39327260414` — `putU32` in the new WAV test writer did not capture
   `putU16`; fixed with a by-reference capture (`261585f`).
3. `39329090509` — `uistate_test` was "Not Run" under ASan+UBSan because
   that job builds an explicit target list; the new target was added
   (this strengthens the gate: the test now runs under the sanitizers)
   (`d4cfcc8`). Result: 7/8 jobs green, `library_test` passed under
   ASan/UBSan including the tempo/duration assertions.

Green runs on the branch: `39331366919` (all 8 jobs, 11m35s) and
`39387321847` (all 8 jobs, 9m54s, after the em-dash fix below).

4. Artifact review of `39331366919`'s screenshots caught a rendering
   defect the harness cannot see (it checks dimensions and brightness,
   not glyphs): the unknown-tempo cell drew `â` plus two boxes instead
   of `—`. Cause: JUCE's `String(const char*)` is an ASCII constructor —
   bytes above 127 are stored per-byte, not decoded as UTF-8. Fixed with
   `juce::String::charToString(0x2014)` (`3217ce8`), which builds the
   codepoint directly. Verified in the `39387321847` shots: a clean em
   dash at both 1920x1080 and 1100x824, with `0:08` durations, folder,
   size and date columns all present.

## 12. Round 3 — browser scroll and per-file zoom memory (W9)

Scope: the W8 residual from §8 — "the stored value is the zoom index
only (no scroll offset, no per-file memory)" — on the same branch and
PR as round 2 (the wave window itself is playhead-following, so there
is no independent wave scroll to store; the scroll state that exists is
the browser list's).

- **Browser scroll (`scroll=`).** JUCE's `ListBox` exposes its position
  as a proportion (`getVerticalPosition` / `setVerticalPosition`), so
  the settings file stores permille (0–1000, integer, clamped at
  parse time) rather than a row index. The stored value is applied
  once, after the first real list population (`pendingScroll` waits
  through empty refreshes); `setVerticalPosition` is a no-op when every
  row already fits. The destructor now writes the settings file, so a
  session that only scrolled still persists the position.
- **Per-file zoom memory (`filezoom=<path>|<idx>`).** Split on the last
  `|` (paths with `=` or spaces survive; a line without a valid zoom is
  dropped, a zoom outside the table clamps). Repeated paths keep one
  entry — the newest wins — and the store is capped at 64 most-recent
  entries so the file cannot grow with the library. Every deck load
  now funnels through `MainComponent::requestLoad`: a stored zoom
  applies at request time (with the save suppressed so the restore is
  not re-recorded), while a file without an entry keeps the deck's
  current zoom; the requested path becomes the memory key that later
  zoom changes record under.
- **Why the shape:** the rules live in the JUCE-free `Source/UiState.h`
  next to the round-2 zoom rules, so the whole thing is gated by the
  extended `Tests/UiStateTest.cpp` (parse defaults, negative/garbage
  scroll, permille clamp, path fidelity, malformed-line drops, table
  clamps, dedupe, 64-entry eviction order, full round trip) without a
  message loop.
- **Smoke impact:** none. The fixtures are loaded before any zoom
  change and no file reloads mid-run, so the memory never applies
  during a run; the harness's final zoom is the default index, so even
  settings written by the lifecycle cycles or at exit hold values
  identical to fresh defaults. Documented in `CI_VISUAL_TESTING.md`.

**Round 3 CI history.** One red run, fixed within budget:
`39396130385` failed only `uistate_test` — a fixture-path typo in the new
serialize/parse round-trip check (`/a=b/…` looked up where the input
wrote `/a=c/…`); fixed in `8b285ac`. The suite then ran green end-to-end:
**`39398458300` — all 8 jobs, 16m08s**, with the extended `uistate_test`
also passing under ASan+UBSan.

**Shot verification.** The 29 frames were pixel-diffed against the last
pre-round-3 green run. Differences are confined to the four
nondeterministic regions documented in `CI_VISUAL_TESTING.md` (clock,
master ring, bottom bar, mixer meters) — the same regions in which two
*pre-round-3* runs differ from each other by twice as many pixels
(1302 vs 686). Round 3 changed no rendered content.

## 13. Round 4 — mixer visual upgrade and bipolar EQ fill fix

Manual review of the round-2/3 release binary reported three defects:
the EQ knobs' displayed value was wrong at rest, a control was
misaligned, and the mixer section did not match the design level of the
upgraded header/browser. Round 4 (`f6fb870`) fixes all three.

**The value defect.** `DJLook::drawRotarySlider`'s bipolar branch
remapped the *pointer* angle so a skewed EQ range (-24..+6 dB, detent at
proportion 0.8) still points at 12 o'clock for 0 dB — but the *fill* arc
was drawn to the raw angle. At rest that raw angle sits 0.3 of the sweep
past the detent, so every EQ knob painted a phantom boost arc from
12:00 to ~2:40 while showing a flat 0 dB. The fill now follows the
remapped pointer angle: flat renders an empty fill, cut and boost grow
symmetrically outward from the detent.

**The misalignment.** The `BLEND` caption was placed at the top of the
crossfader column, which at 1080p was 318 px tall — the caption floated
~124 px above its knob. The xfade row is now a compact 58 px band pinned
to the bottom of the mixer column (DJM-style), with the caption (14 px)
hugging the knob (44 px) and the XFADE label centred on the groove. The
row height is clamped to the available space so the 824 px CI floor
cannot overlap the faders.

**The design gap.** The mixer joined the glass-panel language
(`GlassPanel` backdrop, inset 10x8, matching TopBar/browser). Knobs lost
the near-white plastic discs for dark console bodies with lit rims,
subtler arc tracks (ink@14%, 4 px), and short amber indicators. Faders
gained recessed wells with hairline rims and dark caps with amber
indicator lines (was a plain white pill), and the channel faders became
long-throw (row 160–320 px, fader following the row height). The change
also restyles every other slider consumer (deck tempo, TopBar
MASTER/PHONES, FX-page sends, crossfader) consistently.

**Round 4 CI history.** One push, one run: **`39414595074` — all 8
jobs, 17m04s** (identity, gcc/clang × Debug/Release, ASan+UBSan,
TSan, GUI smoke). Zero red runs; zero correction rounds.

**Shot verification.** All 29 frames from `39414595074` were reviewed:
EQ knobs render an empty fill with pointers up at rest; the glass
panel, dark knobs, long-throw faders, bottom-pinned xfade row and
hugging BLEND caption appear at every viewport including the 1100x824
CI floor (no overlap); TopBar MASTER/PHONES knobs picked up the same
dark-body language. The brightness floor in the harness is unaffected
(dark knobs replaced a small bright area; frames remain far above the
0.1% all-dark threshold).
