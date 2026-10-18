# Contributing to TRANSIT

Thanks for your interest in TRANSIT. **The project is very early
development and not functional** — see the status block in
[`README.md`](README.md) before investing time.

## Ground rules

- License: contributions are accepted under **GPL-3.0-only**
  (`SPDX-License-Identifier: GPL-3.0-only`). Every first-party source file
  carries the project SPDX header; keep it.
- Identity: this repository is TRANSIT by RIG-AUDIO. Legacy product and
  publisher strings from the pre-publication era are not allowed in code,
  docs, filenames, or messages (see the carve-out in
  [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md) for legally
  required font attribution).
- Provenance: no code copied from other DJ applications or GPL sources;
  see [`AGENTS.md`](AGENTS.md) for the full rules.

## Workflow

1. Fork/branch from `main`.
2. Build and test on Linux before opening a pull request:

   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

   Repeat with Clang (`-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`)
   when your change touches DSP, threading, or warnings.
3. Keep changes surgical: no drive-by refactors, no unrelated formatting,
   no new dependencies without discussion.
4. Open a pull request against `main`. CI must be green: GCC/Clang ×
   Debug/Release with warnings-as-errors, plus the full test suite.

## Code expectations

- Zero warnings from first-party code on GCC and Clang; fix types rather
  than casting warnings away.
- Realtime and thread-safety rules from [`AGENTS.md`](AGENTS.md) apply to
  anything reachable from the audio callback.
- Tests: new engine behavior needs a headless CTest gate where practical.
- No secrets, credentials, tokens, or machine-specific absolute paths in
  commits.
