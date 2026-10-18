// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Recorder gates: WAV file is created with a valid header (RIFF, IEEE
// float, stereo), sample count matches, no drops on a short take.
#include "Recorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* name, double got = 0.0, double want = 0.0) {
    std::printf("RECORDER_TEST %s %s (%.4f vs %.4f)\n", cond ? "ok  " : "FAIL",
                name, got, want);
    if (!cond)
        ++failures;
}

uint32_t rdU32(const unsigned char* b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16)
        | ((uint32_t)b[3] << 24);
}

int run() {
    const auto path =
        (std::filesystem::temp_directory_path() / "transit_rec_test.wav").string();
    std::error_code ec;
    std::filesystem::remove(path, ec);

    constexpr int rate = 48000;
    constexpr int frames = 48000;
    std::vector<float> L(frames), R(frames);
    for (int i = 0; i < frames; ++i) {
        L[(size_t)i] = 0.5f * (float)sin(2.0 * 3.141592653589793 * 440.0 * i / rate);
        R[(size_t)i] = -L[(size_t)i];
    }

    lean::Recorder rec;
    check(!rec.isRecording(), "idle at start");
    if (!rec.start(path, rate)) {
        std::printf("RECORDER_TEST FAIL start\n");
        return 1;
    }
    check(rec.isRecording(), "recording after start");
    for (int off = 0; off < frames; off += 512) {
        const int n = std::min(512, frames - off);
        rec.pushBlock(L.data() + off, R.data() + off, n);
    }
    rec.stop();
    check(!rec.isRecording(), "stopped");
    check(rec.droppedFrames() == 0, "no drops", (double)rec.droppedFrames(), 0.0);

    FILE* f = fopen(path.c_str(), "rb");
    check(f != nullptr, "file exists");
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    check(size == 44 + (long)frames * 8, "file size", (double)size,
          (double)(44 + frames * 8));

    unsigned char h[44];
    check(fread(h, 1, 44, f) == 44, "header bytes read");
    check(h[0] == 'R' && h[1] == 'I' && h[2] == 'F' && h[3] == 'F', "riff tag");
    check(h[20] == 3, "ieee float", h[20], 3.0); // format tag low byte
    check(h[22] == 2, "stereo", h[22], 2.0);
    check(rdU32(h + 24) == 48000, "rate", rdU32(h + 24), 48000.0);
    check(rdU32(h + 40) == (uint32_t)frames * 8, "data size",
          rdU32(h + 40), (double)frames * 8);

    // First sample pair roundtrips (sine starts at 0, check frame 100).
    fseek(f, 44 + 100 * 8, SEEK_SET);
    float s[2] = {};
    check(fread(s, sizeof(float), 2, f) == 2, "sample bytes read");
    fclose(f);
    check(std::abs(s[0] - L[100]) < 1e-6 && std::abs(s[1] - R[100]) < 1e-6,
          "sample roundtrip", s[0], L[100]);

    std::filesystem::remove(path, ec);
    if (failures == 0)
        std::printf("RECORDER_TEST PASS\n");
    else
        std::printf("RECORDER_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
