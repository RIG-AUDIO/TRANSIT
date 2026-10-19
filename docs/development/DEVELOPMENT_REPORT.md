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
  checks) in M2. **W7** — verified keep. **W8 persistence** explicitly
  out of scope.
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
- **G6 BPM/duration columns deferred** — Library schema stores only
  path/name/size/mtime; analysis-on-index is a schema change beyond this
  assignment.
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
- **Wave zoom (W8)** does not persist across sessions (out of scope).
- The app binary a human runs locally may be an older artifact; always
  re-download `transit-linux-x64-release` from the latest green run.
- This branch and PR #1 are **not** production-ready: they are a
  reviewed, CI-validated milestone branch pending human merge review.

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
README warning was preserved; PR #1 remains draft and unmerged; the
legacy identity strings remain absent from the tree (scan green in every
run).
