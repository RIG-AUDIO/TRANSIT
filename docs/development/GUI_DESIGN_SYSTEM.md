# TRANSIT — GUI design system

What ships as of milestones M3–M5 (colour roles G2/G7, RIG mark G1,
status row G5, browser rows G6, meters G4, repaint policy G8). This
documents the design *that is in the tree* — the review's spacing/radii/
type-role deliverable (G7) — not a future spec. Findings and status live
in `GUI_AND_STABILITY_REVIEW.md` §3.

## 1. Language

"Modern dark console": deep graphite surfaces, thin lit borders, one amber
accent (transport/active), cool cyan (information), alarm red (record/
clip/danger). No neon glow, no drop shadows, no off-palette blues — the
legacy `tabBlue`/`neonCyan`/`neonPink` tokens were deleted with G2.

## 2. Colour tokens (`Source/DJLook.{h,cpp}`)

| Token | Hex | Role |
|---|---|---|
| `bg` | `#14171C` | window background (subtle vertical gradient `bg.brighter(0.06)` → `bg`); also the text colour on cyan/alarm fills |
| `panel` | `#1D2229` | section panels; the RIG badge disc |
| `deep` | `#0A0D10` | deepest canvas: deck waveform area, top-bar master strip, output-meter well |
| `well` | `#161A20` | recessed rows: channel VU background, overview stripe |
| `accent` | `#FFB454` | amber — active/transport: section headers, knob caps' arc, playheads, filled sliders, `READY` chip, "To A/B" toggles |
| `cyan` | `#5FD4FF` | information: waveforms, MIX pill fill, selection pill, `LOADING/ANALYZING` chips, level bars below clip |
| `alarm` | `#FF5252` | danger: `ERROR` chip + payload, meter clip tail, REC-on fill |
| `ink` | `#E8ECF1` | primary text: wordmark, deck headers, filenames, unselected rows, peak-hold hairlines |
| `dimInk` | `#8B95A3` | secondary text: control captions, off-tab labels, metadata columns, `EMPTY` chip, hints |

Derived, but fixed (also tokens of the system):

- glass panel body gradient `#181D26` → `#0B0D11` + a `white@7%` 1 px top
  highlight (`DJLook::drawGlassPanel`);
- lit border: `ink@20%`, 1.0 px (`DJLook::drawLitBorder`);
- button hover fill `#1C2230` (inside `DJLook::drawButtonBackground`);
- RIG badge rim: `ink@30%`, 1.5 px-equivalent stroke in the SVG
  (`Source/Assets/rig_mark.svg`, letters `ink`, disc `panel`).

## 3. Type roles (Adriatica Next, embedded via `TRANSITAssets`/OFL)

All role fonts are **bold** (`.boldened()`); sizes in logical px:

| Size | Role | Colour |
|---|---|---|
| 24 | TopBar wordmark "TRANSIT" | `ink` |
| 17 | deck header (`DECK A \| 120.0 BPM`) | `accent` |
| 16 | Settings dialog title | (dialog default) |
| 15 | section headers `LIBRARY`, `MIXER` | `accent` |
| 14 | AUX strip headers (FX page, centred) | `accent` |
| 13 | channel caps `A`/`B` (centred) | `accent` |
| 11 | control captions (knob caps, mixer caps, AUX caps), **state chip** | `dimInk` (chip recolours per state) |
| 10 | TopBar caps (`MASTER`, `PHONES`) | `dimInk`/context |

Unset sizes (transport times, fader values, browser rows, status payload,
sys status) fall back to the LookAndFeel default size; their **colours**
are still token-bound (`ink`/`dimInk`/`alarm`/`accent` as roleled below).

## 4. Radii and strokes

| Radius | Where |
|---|---|
| 1 px (stroke) | lit borders, button outlines (1.5 px when highlighted), glass top highlight |
| 2 px | slider thumbs and grooves (`DJLook::drawLinearSlider`) |
| 3 px | meter well + meter fill, small wells |
| **4 px** | house radius: glass panels (TopBar, `GlassPanel`), their lit borders, FileList selection pill, list-row pills |
| 5 px | button/combo bodies, expanded hover halo (`area.expanded(2.0f)`) |

## 5. Spacing / structure (layout constants)

- Window: resizable, `setResizeLimits(1100, 824, 16384, 16384)`; opens
  fullscreen; F11 toggles, Escape leaves (G3 floor = smallest CI viewport).
- TopBar 64 px: RIG badge box `(12, 10, 44, 44)` (optical centre 34,32),
  wordmark, tab row (MIX/FX/SAMPLER/LOOP/REC), master history strip,
  `MASTER`/`PHONES` knobs, `SETUP`, window buttons, clock.
- Deck column: header row 28, button row 30 (`Open 64 / Play 52 / CUE 52 /
  Key 52`, gaps 5), status row 22 (**state chip 86** + payload), time row
  20 (`←/→` 40), tempo row 30, waveform fills the rest; region gap 12.
- Mixer: header 28, channel-cap row 18 (cap column 56), knob rows 44,
  fader row 190, channel VUs 14 px wide.
- Browser: fixed 220 px band + 8 px gap; meter strip zone 34 px (paint
  area 26 px, inset 12×7, radius 3); page reserves 34 px at the bottom.
- Overview stripe: 14 px, only when the waveform view is ≥48 px tall.
- FileList rows: pill inset (4, 2) radius 4, text from x = 12; columns
  `name (flex) | folder 96 | size 78 | date 92`, extras drop below 480/320
  px row width instead of clipping (G6, G3 spirit).

## 6. Component language

- **Glass panels** — `drawGlassPanel` + `drawLitBorder` at radius 4; used
  by the TopBar and anything adopting the panel language (`GlassPanel`
  base class).
- **Tab pills** (G2) — flat fills, no glow: on-tab = cyan fill (REC = alarm
  while recording) with `bg` text (the `TextButton::textColourOnId`
  default); off-tab = no fill, `dimInk` text. `MIX` (library+decks view)
  and `FX` (AUX/send page) are live view toggles; `SAMPLER`/`LOOP` are
  parked — disabled with a "Not in this build" tooltip (G9, unchanged);
  `REC` arms recording.
- **Status row** (G5) — bold 11 px state chip + payload label per deck:
  `EMPTY` `dimInk` / `LOADING..` `cyan` with a 3 Hz animated ellipsis /
  `ANALYZING` `cyan` / `READY` `accent` + filename in `ink` / `ERROR`
  `alarm` + engine error text in `alarm`. Text and colour update only on
  change.
- **Meters** (G4) — everything renders in a −60…0 dB unit space
  (`DJLook::meterUnit`): channel VUs (14 px, cyan → amber@0.6 → alarm
  ramp + ink peak-hold hairline), master meter (bottom strip, amber@0.7,
  alarm tip, hold marker), top-bar master history (96 bins, clip colour
  on the raw peak >0.98). Constant ~18 dB/s bar fall, ~0.75 s hold then
  ~14 dB/s marker fall (`MeterHold`).
- **Browser** (G6) — `name | bpm | time | folder | size | added-date`
  columns, precomputed (paint is I/O-free), Enter or double-click loads to
  the selected deck (`To A`/`To B`), flat cyan selection pill (row 0
  seeded in CI), S6-debounced search over the SQLite index; folder choice
  shows `path - scanning...` while the S6 worker indexes. The tempo and
  duration columns come from the library index (analysed at scan time);
  rows outside the index fall back to a header-only duration probe and
  show `—` for an unknown tempo. The metadata columns drop out on narrow
  lists (time < 640 px, bpm < 760 px, folder < 320 px, size/date < 480 px)
  instead of clipping the file name.
- **Decks** — waveform view with overview stripe, `−/+` zoom (4 s…whole),
  `SYNC`, transport (`Play/CUE/Key`), tempo/gain/EQ/fader controls in the
  mixer; drop target + `Drop a track - double-click the library` hint.

## 7. Out of scope by rule

No new DJ subsystems (loops/samplers/hotcues), no dependency upgrades, no
merges to `main`. "Vintage" theme subclassing the same LookAndFeel
contract is a README-future, not started. Spacing/radii/type values above
are **observed from shipped code** — nothing is half-invented here; any
future extension should add tokens to `DJLook` first, then use them.
