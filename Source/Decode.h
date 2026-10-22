// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>
#include <vector>

namespace lean {

// Whole-file decode to interleaved stereo float. The output keeps the file's
// native sample rate; rate conversion happens in Deck at render time so the
// signal is converted exactly once.
//
// WAV/MP3/FLAC/Ogg via miniaudio. AAC/M4A via OS codecs (Windows
// MediaFoundation; other platforms report an error until wired up).
struct DecodedAudio {
    double sampleRate = 0.0;
    std::vector<float> stereo; // interleaved LRLR...
};

// utf8Path: UTF-8 encoded absolute path. Returns true on success, false with
// a human-readable message in error otherwise. Thread-safe; allocates freely
// (call from message/worker threads, never the audio thread).
bool decodeFile(const std::string& utf8Path, DecodedAudio& out, std::string& error);

} // namespace lean
