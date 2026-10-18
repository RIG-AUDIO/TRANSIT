# TRANSIT — Third-Party Notices

TRANSIT is licensed under GPL-3.0-only (see `LICENSE`). This file lists the
third-party components the source tree fetches or vendors, together with
their licenses and canonical sources. License texts for components that
redistribute their license alongside the code are kept in the tree where
noted.

## Fetched at configure time (CMake FetchContent)

| Component | Version | License | Source |
|---|---|---|---|
| JUCE | 9.0.1 (pinned tag) | GPLv3 / commercial dual license | https://github.com/juce-framework/JUCE — license terms: https://juce.com/legal/ |
| miniaudio | 0.11.21 | MIT (public-domain alternative) | https://github.com/mackron/miniaudio |
| signalsmith-stretch | 1.4.0 | MIT | https://github.com/Signalsmith-Audio/signalsmith-stretch |
| SQLite (amalgamation) | 3.51.1 | Public domain | https://www.sqlite.org/2025/sqlite-amalgamation-3510100.zip |

Notes:

- **JUCE** is a dual-licensed dependency. TRANSIT is distributed under
  GPL-3.0-only, which matches JUCE's GPLv3 option. JUCE's own bundled
  third-party sources ship inside the JUCE repository under the terms JUCE
  distributes them with. Developers may alternatively point the build at a
  locally licensed JUCE checkout via `TRANSIT_JUCE_ROOT`.
- **miniaudio** vendors its supporting decoders (dr_mp3, dr_wav, dr_flac,
  etc.) inside its single-header distribution; see the upstream repository
  for the full component list and license.
- **signalsmith-stretch** pulls its MIT-licensed STFT dependency through its
  own CMakeLists at configure time.
- **SQLite** is in the public domain.

## Vendored in the source tree

| Asset | License | Location |
|---|---|---|
| Adriatica Next (Medium) | SIL Open Font License 1.1 | `Source/Assets/AdriaticaNext-Medium.ttf` |
| Adriatica Next Condensed (Bold) | SIL Open Font License 1.1 | `Source/Assets/AdriaticaNext-CondensedBold.ttf` |

The full OFL 1.1 text and the fonts' required attribution are kept verbatim
in `Source/Assets/OFL.txt` and in the fonts' embedded name tables:

> Copyright (c) 2026 MontroneDSP. Portions derived from Fira Sans,
> Copyright (c) 2012-2015 The Mozilla Foundation and Telefonica S.A.

**Attribution carve-out:** this copyright line is a legally required part of
the OFL license and is retained unmodified as such. It is historical
attribution, not the current publisher identity of TRANSIT (RIG-AUDIO), and
it is deliberately excluded from branding-cleanup claims.

## Indirect dependencies

JUCE, miniaudio, signalsmith-stretch, and SQLite are consumed as upstream
projects; their internal dependency graphs travel with the pinned upstream
versions above. Any change to the pins must be accompanied by a review of
the corresponding upstream license terms.
