// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace lean {

// Tempo estimate in BPM, 0.0 when unknown. Original implementation:
// mono mix -> rectify -> one-pole envelope (~8 Hz) -> downsample to 200 Hz
// -> mean-removed autocorrelation over lags for 60..200 BPM -> peak pick
// with half/double-time disambiguation favoring the dance range.
inline double detectBpm(const float* interleavedStereo, int numFrames,
                        double sampleRate) {
    if (!interleavedStereo || numFrames <= 0 || sampleRate <= 0.0)
        return 0.0;

    // Envelope at ~200 Hz.
    constexpr double envRate = 200.0;
    const int step = std::max(1, (int)(sampleRate / envRate));
    const size_t nEnv = (size_t)numFrames / (size_t)step;
    if (nEnv < (size_t)(envRate * 4)) // need at least ~4 s for 60 BPM
        return 0.0;

    // One-pole ~8 Hz smoother on the envelope clock (not the audio clock).
    const double alpha = 1.0 - exp(-2.0 * 3.141592653589793 * 8.0 / envRate);
    std::vector<double> env(nEnv, 0.0);
    double lp = 0.0;
    for (size_t i = 0; i < nEnv; ++i) {
        double peak = 0.0;
        const size_t from = i * (size_t)step;
        const size_t to = std::min(from + (size_t)step, (size_t)numFrames);
        for (size_t f = from; f < to; ++f) {
            const double m = 0.5
                * (std::abs(interleavedStereo[f * 2])
                   + std::abs(interleavedStereo[f * 2 + 1]));
            peak = std::max(peak, m);
        }
        lp += alpha * (peak - lp);
        env[i] = lp;
    }

    double mean = 0.0;
    for (double v : env)
        mean += v;
    mean /= (double)nEnv;
    double var = 0.0;
    for (double& v : env) {
        v -= mean;
        var += v * v;
    }
    if (var <= 1e-12)
        return 0.0;

    // Lag range for 60..200 BPM at envRate.
    const int lagMin = (int)(60.0 * envRate / 200.0);
    const int lagMax = (int)(60.0 * envRate / 60.0);
    auto corr = [&](int lag) {
        double s = 0.0;
        for (size_t i = 0; i + (size_t)lag < nEnv; ++i)
            s += env[i] * env[i + (size_t)lag];
        return s / var;
    };

    int bestLag = lagMin;
    double best = -1.0;
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        const double c = corr(lag);
        if (c > best) {
            best = c;
            bestLag = lag;
        }
    }
    double bpm = 60.0 * envRate / bestLag;

    // Half-time reports are the classic error: if the double-time lag also
    // correlates strongly, prefer the faster (dance-range) reading.
    if (bpm < 100.0) {
        const int dbl = bestLag / 2;
        if (dbl >= lagMin && corr(dbl) > 0.6 * best)
            bpm *= 2.0;
    } else if (bpm > 170.0) {
        const int half = bestLag * 2;
        if (half <= lagMax && corr(half) > 0.8 * best)
            bpm *= 0.5;
    }
    if (bpm < 59.0 || bpm > 201.0)
        return 0.0;
    return bpm;
}

} // namespace lean
