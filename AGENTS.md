# TRANSIT — agent and contributor rules

1. **Identity.** This repository is TRANSIT, published by RIG-AUDIO
   (`https://github.com/RIG-AUDIO/TRANSIT`), licensed GPL-3.0-only. Never
   reintroduce the legacy product, publisher, or developer-identity strings
   from the pre-publication era (case-insensitive). The canonical patterns
   live in `.githooks/pre-push` and `.github/workflows/linux.yml`; CI and
   the pre-push hook enforce them. The only permitted in-tree occurrences
   are legally required font attribution (`Source/Assets/OFL.txt`,
   `docs/THIRD_PARTY_NOTICES.md`, the fonts' embedded name tables) and the
   technical `lean::` namespace.
2. **Git/gh discipline.** Use `scripts/transit-git.sh` and
   `scripts/transit-gh.sh` (identity guards) instead of bare `git`/`gh` for
   repo operations. Never force-push, never print credentials, never commit
   tokens or local paths. Push and commits require explicit human approval.
3. **Clean-room provenance.** Never read or copy code from other GPL DJ
   applications while working here (the legacy workspace also enforced
   this against a locally mirrored upstream). Black-box behavior
   comparison only. Permitted inputs: official API docs (JUCE, miniaudio),
   MIT/BSD-licensed component docs, and code written from scratch.
4. **Licensing.** Every first-party source file starts with
   `// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only`
   (`#` comment style in CMake/scripts). New dependencies must be
   GPL-3.0-compatible, pinned, and recorded in
   `docs/THIRD_PARTY_NOTICES.md`. Do not edit `LICENSE`, `OFL.txt`, or font
   metadata.
5. **Engine stays JUCE-free.** DSP under `Source/` (`Deck.*`, `Mixer.*`,
   `Decode.*`, `Recorder.*`, analysis headers) must not include JUCE.
   Shell/UI code may use JUCE.
6. **Realtime rules for the audio path:** no allocation, locking, logging,
   or I/O in steady-state `render()` / the device callback. Preallocate at
   load/prepare. Two documented, bounded exceptions, both user-initiated
   only: (a) buffer pickup and stretcher reconfiguration run at the next
   block boundary on the audio thread (one bounded allocation, e.g. on file
   load or keylock toggle); (b) a retired audio buffer may be freed on the
   audio thread when its last reference drops (single free, only on load).
7. **Thread-safety rules:** every scalar crossing message<->audio threads
   is `std::atomic` (compile-time `static_assert` on lock-free); buffers
   cross by staged `shared_ptr` + audio-side `try_lock` pickup; EQ
   coefficients rebuild on the audio thread from atomic dB values;
   continuous controls are `SmoothFloat` (atomic target, audio-side
   follower). Tests must snap (`Mixer::snap`) before measuring.
8. **Zero warnings.** First-party targets build with the strict flag set in
   `CMakeLists.txt` (`-Wall -Wextra -Wpedantic -Wconversion
   -Wsign-conversion -Wshadow -Wformat=2 -Wundef -Wnon-virtual-dtor
   -Woverloaded-virtual -Wimplicit-fallthrough -Werror` on GCC/Clang; `/WX`
   on MSVC). Never silence diagnostics with casts that hide truncation —
   fix the types. Third-party code stays behind system includes or
   separate targets; never add third-party sources to the first-party
   warning list.
9. **Test gates.** All CTest tests must pass on Linux with GCC and Clang
   (`ctest --test-dir build --output-on-failure`). Any change to `Deck`
   must keep `null_render` passing (< -90 dBFS at unity).
10. **Linux first.** Validate on Linux before considering a change done;
    CI (`.github/workflows/linux.yml`) is the authority on GCC/Clang ×
    Debug/Release.
