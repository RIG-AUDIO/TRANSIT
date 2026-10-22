# Security Policy

## Status

TRANSIT is pre-release: **very early development, not functional.** There
are no supported releases, no long-term support branches, and no security
guarantees. If a version is unlisted below, it is unsupported.

| Version | Supported |
|---|---|
| pre-release `main` | ❌ no |

## Reporting a vulnerability

Please do **not** open a public issue for security-sensitive reports.

- Use GitHub's private vulnerability reporting for
  [`RIG-AUDIO/TRANSIT`](https://github.com/RIG-AUDIO/TRANSIT)
  (**Security → Report a vulnerability**), which delivers the report
  privately to the maintainers.
- If private vulnerability reporting is unavailable, open a public issue
  that contains **no exploit details** and asks the maintainers to enable
  it or provide a private contact channel.

Include: affected commit/tag, environment (OS, compiler), impact,
reproduction steps, and any known mitigations.

## Scope notes

- TRANSIT handles audio files and MIDI input but is not a networked
  service; still, malformed media files are in scope (decoder robustness).
- The project fetches pinned build dependencies at configure time
  (JUCE, miniaudio, signalsmith-stretch, SQLite); reports about those
  belong upstream, but a pin we should bump is welcome here.
