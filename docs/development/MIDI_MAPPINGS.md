# TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

# MIDI mappings

Factory device maps live in `Source/Flx4Map.h` as pure data tables plus
small decode helpers. They are JUCE-free and header-only so the hardware
facts are gated by plain unit tests (`Tests/MidiMapTest.cpp`). Learned
bindings (the Setup dialog's Learn flow, persisted to `midimap.txt`)
coexist with a factory map: the learned grid is replayed first, then the
device table installs on top; a learned bind that collides with a table
cell wins until the next routing pass.

## Pioneer DDJ-FLX4

Channels here are 1-based (JUCE convention): hardware deck 1 = ch 1,
deck 2 = ch 2, mixer/browse = ch 7. Value controls are standard 14-bit
CC pairs (MSB on `cc`, LSB on `cc + 0x20`); buttons are notes; jog and
browse encoders are 7-bit relative CCs (0x40 = centre, +1 per detent,
two's complement for CCW).

### Values (14-bit pairs)

| Control | Ch | MSB CC | Target | Notes |
|---|---|---|---|---|
| Tempo (deck 1/2) | 1 / 2 | 0x00 | `Tempo` | |
| Trim / Gain | 1 / 2 | 0x04 | `Gain` | |
| EQ High | 1 / 2 | 0x07 | `High` | |
| EQ Mid | 1 / 2 | 0x0B | `Mid` | |
| EQ Low | 1 / 2 | 0x0F | `Low` | |
| Channel fader | 1 / 2 | 0x13 | `Fader` | |
| Crossfader | 7 | 0x1F | `Xfade` | |
| Phones mix (BLEND) | 7 | 0x0C | `Blend` | |
| CFX 1 | 7 | 0x17 | `Send` deck 0 | hardware filter position -> AUX send 0 |
| CFX 2 | 7 | 0x18 | `Send` deck 1 | hardware filter position -> AUX send 0 |

### Actions (notes / relative CCs)

| Control | Kind | Ch | Num | Action |
|---|---|---|---|---|
| PLAY | note | 1 / 2 | 0x0B | `Play` (toggle on press) |
| CUE | note | 1 / 2 | 0x0C | `Cue` (preview while held) |
| SYNC | note | 1 / 2 | 0x58 | `Sync` (toggle) |
| PFL | note | 1 / 2 | 0x54 | `Pfl` (toggle) |
| SHIFT | note | 1 / 2 | 0x3F | `Shift` (state only) |
| LOAD | note | 7 | 0x46 / 0x47 | `Load` selected library row to deck 1 / 2 |
| Browse press | note | 7 | 0x41 | `BrowsePress` (Enter: load selection) — *verify* |
| Browse encoder | CC | 7 | 0x40 | `BrowseScroll` (relative; moves list selection) |
| Jog (edge / platter) | CC | 1 / 2 | 0x21, 0x22, 0x23 | `JogNudge` (temporary +/-6% bend) |
| SHIFT + jog | CC | 1 / 2 | 0x29 | `JogSearch` (jump by beats) |

### LEDs (MIDI out)

Play 0x0B, Cue 0x0C, Sync 0x58, Pfl 0x54 on the per-deck channel. The
message thread echoes the app state (play, cue-ready, sync, pfl) with a
last-sent cache so only changes go out. A position-resync SysEx
(`F0 00 40 05 00 00 04 05 00 50 02 F7`) is sent once on activation so a
fresh connection reports every analog control's position and keeps the
link alive.

### Soft takeover

Absolute controls are gated by a per-cell pickup while the FLX4 map is
live: the hardware is silent until a control is moved, so the first
gesture must not jump the app value. A cell engages when the hardware
reaches or crosses the current app value, then follows every later tick.
A control parked at full travel (endpoint) engages on the first tick, so
a fader or knob left at an extreme does not require a full sweep through
the app value to take control.

Center-detented hardware knobs (trim, EQ) are remapped before the pickup
engage: the FLX4 detent sends 0.5, but the app's detent for those targets
sits at 0.8 normalized (gain unity = 1.0/1.25; EQ 0 dB = 24/30 on the
−24..+6 range). A piecewise-linear remap maps each half of the hardware
travel onto the app's range so the first touch lands exactly on the app
value. Faders (tempo, channel, crossfader) use the plain linear mapping.

Learned (non-FLX4) controls keep their existing immediate behaviour.

### Out of scope (not mapped yet)

Pad modes (hot cues, loops, beat jump, pad FX), the BEAT FX section,
phones level (CC unknown — *verify* on hardware), scratch-style jog
positioning (nudge only), and non-CC learn targets. CFX knobs ride the
per-deck AUX send as a creative mapping; the hardware sends a filter
position, not a send level.

## Provenance

Control-to-byte bindings above are hardware facts from AlphaTheta's
official "DDJ-FLX4 MIDI message List" (Ver 1.0), corroborated by
community hardware-monitor reverse engineering. No code or creative
expression from any third-party mapping is used; the pickup, mailbox,
folding and table layout are original TRANSIT logic. Rows marked
*verify* need a pass with a MIDI monitor on real hardware.
