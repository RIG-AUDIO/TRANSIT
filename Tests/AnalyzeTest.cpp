// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// BPM detector gates: click tracks at 120 and 128 BPM read correctly,
// silence/noise reads unknown (0.0), short clips read unknown.
#include "Analyze.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
int failures = 0;

void check(bool cond, const char* name, double got, double want) {
    std::printf("ANALYZE_TEST %s %s (got %.2f want %.2f)\n",
                cond ? "ok  " : "FAIL", name, got, want);
    if (!cond)
        ++failures;
}

// 8 s click track: 1 kHz pips, 20 ms, on every beat.
std::vector<float> clickTrack(double bpm) {
    const int frames = (int)kRate * 8;
    std::vector<float> buf((size_t)frames * 2, 0.0f);
    const double beat = 60.0 / bpm;
    for (double t = 0.0; t < 8.0; t += beat) {
        const int start = (int)(t * kRate);
        const int len = (int)(kRate * 0.02);
        for (int i = 0; i < len && start + i < frames; ++i) {
            const float s =
                0.6f * (float)sin(2.0 * 3.141592653589793 * 1000.0 * i / kRate);
            buf[(size_t)(start + i) * 2] = s;
            buf[(size_t)(start + i) * 2 + 1] = s;
        }
    }
    return buf;
}

int run() {
    {
        auto t120 = clickTrack(120.0);
        const double bpm = lean::detectBpm(t120.data(), (int)(t120.size() / 2), kRate);
        check(std::abs(bpm - 120.0) < 1.0, "120 bpm", bpm, 120.0);
    }
    {
        auto t128 = clickTrack(128.0);
        const double bpm = lean::detectBpm(t128.data(), (int)(t128.size() / 2), kRate);
        check(std::abs(bpm - 128.0) < 1.5, "128 bpm", bpm, 128.0);
    }
    {
        std::vector<float> silence((size_t)(int)kRate * 8 * 2, 0.0f);
        const double bpm =
            lean::detectBpm(silence.data(), (int)(silence.size() / 2), kRate);
        check(std::abs(bpm) < 1e-12, "silence unknown", bpm, 0.0);
    }
    {
        auto tiny = clickTrack(120.0);
        const int frames = (int)kRate * 2; // too short for 60 BPM
        const double bpm = lean::detectBpm(tiny.data(), frames, kRate);
        check(std::abs(bpm) < 1e-12, "short clip unknown", bpm, 0.0);
    }
    if (failures == 0)
        std::printf("ANALYZE_TEST PASS\n");
    else
        std::printf("ANALYZE_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
