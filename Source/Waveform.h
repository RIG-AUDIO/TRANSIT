// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace lean {

// Downsampled peak overview for waveform display. Pure C++, no dependencies.
// peaks[i] = maximum absolute sample over its window (mono max of L/R).
struct Waveform {
    std::vector<float> peaks;
    double secondsPerPeak = 0.0;
};

inline Waveform computePeaks(const float* interleavedStereo, int numFrames,
                             double sampleRate, int targetPeaks = 2048) {
    Waveform w;
    if (!interleavedStereo || numFrames <= 0 || sampleRate <= 0.0)
        return w;
    const size_t n = (size_t)std::clamp(targetPeaks, 64, 16384);
    w.peaks.assign(n, 0.0f);
    w.secondsPerPeak = (double)numFrames / sampleRate / (double)n;
    for (size_t i = 0; i < n; ++i) {
        const int from = (int)((long long)i * numFrames / (long long)n);
        const int to = (int)((long long)(i + 1) * numFrames / (long long)n);
        float peak = 0.0f;
        for (int f = from; f < to; ++f) {
            peak = std::max(peak, std::abs(interleavedStereo[(size_t)f * 2]));
            peak = std::max(peak, std::abs(interleavedStereo[(size_t)f * 2 + 1]));
        }
        w.peaks[i] = peak;
    }
    return w;
}

} // namespace lean
