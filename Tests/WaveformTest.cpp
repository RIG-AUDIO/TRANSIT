// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Waveform peak-overview gates: constant tone gives flat peaks at the right
// level, silence gives zeros, and sizing/clamping behave.
#include "Waveform.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* name, double got, double want) {
    std::printf("WAVEFORM_TEST %s %s (got %.4f want %.4f)\n",
                cond ? "ok  " : "FAIL", name, got, want);
    if (!cond)
        ++failures;
}

int run() {
    constexpr double rate = 48000.0;
    constexpr int frames = (int)rate * 4;
    std::vector<float> tone((size_t)frames * 2);
    for (int i = 0; i < frames; ++i) {
        const float s =
            0.5f * (float)sin(2.0 * 3.141592653589793 * 440.0 * i / rate);
        tone[(size_t)i * 2] = s;
        tone[(size_t)i * 2 + 1] = s * 0.5f;
    }

    auto w = lean::computePeaks(tone.data(), frames, rate, 1024);
    check((int)w.peaks.size() == 1024, "peak count", (double)w.peaks.size(), 1024.0);
    double mean = 0.0;
    for (float p : w.peaks)
        mean += p;
    mean /= (double)w.peaks.size();
    // A full sine period fits many times per window, so every peak ~0.5.
    check(std::abs(mean - 0.5) < 0.01, "tone peak level", mean, 0.5);
    check(std::abs(w.secondsPerPeak - (4.0 / 1024)) < 1e-9, "seconds per peak",
          w.secondsPerPeak, 4.0 / 1024);

    std::vector<float> silence((size_t)frames * 2, 0.0f);
    auto ws = lean::computePeaks(silence.data(), frames, rate, 256);
    double peakMax = 0.0;
    for (float p : ws.peaks)
        peakMax = std::max(peakMax, (double)p);
    check(std::abs(peakMax) < 1e-12, "silence peaks", peakMax, 0.0);

    auto empty = lean::computePeaks(nullptr, 0, rate, 1024);
    check(empty.peaks.empty(), "empty input", (double)empty.peaks.size(), 0.0);

    if (failures == 0)
        std::printf("WAVEFORM_TEST PASS\n");
    else
        std::printf("WAVEFORM_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
