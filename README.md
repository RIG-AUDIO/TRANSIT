# TRANSIT — DJ Software

> **Status: very early development — not functional.**
>
> TRANSIT cannot yet be used as a DJ application. There are no releases, no
> supported workflows, and no guarantees that anything you build will run on
> your machine. Builds and tests in this repository are engineering
> milestones only; they do **not** mean the product works. Expect breakage,
> missing features, and data loss. Do not use it in a live setting. By
> building or running this software you accept that it is unsupported
> developer preview code.

TRANSIT is a lean, Linux-first DJ application. It is the public continuation
of a clean-room effort started from the Dance plugin port foundation: a small
C++20/`lean::` core (decks, mixer, decoding, sync, waveform analysis, MIDI
mapping, recording) with a JUCE-based graphical front end.

- **Publisher:** RIG-AUDIO — https://github.com/RIG-AUDIO/TRANSIT
- **License:** GPL-3.0-only — see [`LICENSE`](LICENSE)
- **Platform priority:** Linux first (GCC and Clang), warnings are errors in
  first-party code

## Dependencies and licenses

Build dependencies: CMake ≥ 3.22, Ninja (or any C++20 generator), a C++20
compiler, and Linux GUI/audio development headers (JUCE + miniaudio).

Runtime/configure dependencies are fetched at configure time and pinned:
JUCE 9.0.1, miniaudio 0.11.21, signalsmith-stretch 1.4.0, SQLite 3.51.1.
UI fonts are vendored under the SIL Open Font License 1.1. Full license
details and required attributions: [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md).

## Build (Linux)

```sh
git clone https://github.com/RIG-AUDIO/TRANSIT.git
cd TRANSIT

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements for the system packages (Debian/Ubuntu family names):

```sh
sudo apt install build-essential cmake ninja-build \
  libasound2-dev libjack-jackd2-dev libfreetype6-dev libx11-dev \
  libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libgl1-mesa-dev libcurl4-openssl-dev
```

JUCE is fetched automatically as a pinned public dependency. Developers with
a private JUCE 9.0.1 checkout can point the build at it instead:

```sh
cmake -S . -B build -DTRANSIT_JUCE_ROOT=/path/to/JUCE-9.0.1
# or: export TRANSIT_JUCE_ROOT=/path/to/JUCE-9.0.1
```

### Warning discipline

First-party sources build with a strict, warnings-as-errors flag set
(`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
-Wformat=2 -Wundef -Wnon-virtual-dtor -Woverloaded-virtual
-Wimplicit-fallthrough -Werror` on GCC/Clang). Third-party code is consumed
through system includes or separate targets and is never gated by these
flags. See `CMakeLists.txt` for the authoritative list.

## Tests

Headless gates run via CTest (no audio hardware or display required):

```sh
ctest --test-dir build --output-on-failure
```

Manual developer probes (not gates): `DspBench` (DSP load),
`ListAudioTypes` (visible devices), `UiShot` (offscreen UI snapshot),
`FxProbe` (VST3 load/process/unload on this machine).

## Repository notes

- CI: `.github/workflows/linux.yml` builds GCC/Clang × Debug/Release and
  runs the test suite on every push and pull request.
- Contributor rules for automated agents and humans:
  [`AGENTS.md`](AGENTS.md). Contribution guidelines: [`CONTRIBUTING.md`](CONTRIBUTING.md).
- Security reporting: [`SECURITY.md`](SECURITY.md).
