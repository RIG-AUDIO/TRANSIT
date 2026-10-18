// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace lean {

// Multiresolution waveform model (W1/W2/W6): per-level signed {min,max}
// pairs built once off the audio thread, immutable after build. Pure C++,
// no dependencies.
//
// levels[0] is the finest grid (target 0.5 ms buckets, bounded count), each
// further level halves (ceil) the previous bucket count down to one bucket.
// The view aggregates min/max per pixel from the coarsest level whose bucket
// still fits the pixel span (1-2 buckets per pixel -> O(viewport) work per
// frame, no point-sampling misses); when even level 0 is coarser than the
// span (absurdly long tracks) level 0 is the best available and is used.
struct Waveform {
    struct Level {
        std::vector<float> mins;
        std::vector<float> maxs;
    };

    std::vector<Level> levels; // [0] finest ... back() coarsest (one bucket)
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
    std::int64_t frames = 0;

    bool valid() const {
        return !levels.empty() && !levels[0].mins.empty()
               && levels[0].mins.size() == levels[0].maxs.size()
               && durationSeconds > 0.0;
    }

    void reset() {
        levels.clear();
        sampleRate = 0.0;
        durationSeconds = 0.0;
        frames = 0;
    }

    double bucketSeconds(std::size_t level) const {
        if (levels.empty() || level >= levels.size()
            || levels[level].mins.empty())
            return 0.0;
        return durationSeconds / (double)levels[level].mins.size();
    }

    // Signed min/max of the mixed-down mono signal over [t0, t1] seconds
    // (clamped to the track; a zero-width range resolves to its containing
    // bucket). Single source of truth for paint and seek-time tests.
    void aggregate(double t0, double t1, float& outMin, float& outMax) const {
        outMin = 0.0f;
        outMax = 0.0f;
        if (!valid())
            return;
        t0 = std::clamp(t0, 0.0, durationSeconds);
        t1 = std::clamp(t1, 0.0, durationSeconds);
        if (t1 < t0)
            std::swap(t0, t1);
        const double span = t1 - t0;
        // Coarsest level whose bucket still fits the span; fall back to the
        // finest level when no bucket is small enough (long-track cap).
        std::size_t use = 0;
        for (std::size_t k = levels.size(); k-- > 0;) {
            use = k;
            if (bucketSeconds(k) <= span)
                break;
        }
        const Level& lv = levels[use];
        const double bd = bucketSeconds(use);
        const std::int64_t n = (std::int64_t)lv.mins.size();
        // The 1e-9 nudge keeps exact-boundary windows out of the next bucket
        // when a double lands a hair off the grid.
        std::int64_t i0 = (std::int64_t)std::floor(t0 / bd + 1e-9);
        std::int64_t i1 = (std::int64_t)std::ceil(t1 / bd - 1e-9) - 1;
        i0 = std::clamp<std::int64_t>(i0, 0, n - 1);
        i1 = std::clamp<std::int64_t>(i1, 0, n - 1);
        if (i1 < i0)
            i1 = i0;
        float mn = lv.mins[(std::size_t)i0];
        float mx = lv.maxs[(std::size_t)i0];
        for (std::int64_t i = i0 + 1; i <= i1; ++i) {
            mn = std::min(mn, lv.mins[(std::size_t)i]);
            mx = std::max(mx, lv.maxs[(std::size_t)i]);
        }
        outMin = mn;
        outMax = mx;
    }
};

// Builds the pyramid from interleaved stereo. Rejects null data, non-positive
// frame counts, non-finite/non-positive rates and absurd durations (> 24 h at
// the declared rate: corrupted headers) explicitly; non-finite samples are
// excluded from the payload rather than propagated. Mono mix-down first (W1):
// the display envelope shows what the master sum would be audible.
inline Waveform buildWaveform(const float* interleavedStereo,
                              std::int64_t numFrames, double sampleRate) {
    Waveform w;
    if (!interleavedStereo || numFrames <= 0 || !(sampleRate > 0.0))
        return w;
    const double seconds = (double)numFrames / sampleRate;
    if (!std::isfinite(seconds) || seconds > 86400.0)
        return w;
    w.sampleRate = sampleRate;
    w.durationSeconds = seconds;
    w.frames = numFrames;

    // Level 0: 0.5 ms target buckets, bounded, never more buckets than frames.
    std::int64_t n = (std::int64_t)(seconds * 2000.0);
    n = std::clamp<std::int64_t>(n, 1024, 524288);
    n = std::min<std::int64_t>(n, numFrames);

    Level lvl0;
    lvl0.mins.assign((std::size_t)n, std::numeric_limits<float>::max());
    lvl0.maxs.assign((std::size_t)n, -std::numeric_limits<float>::max());
    for (std::int64_t f = 0; f < numFrames; ++f) {
        const float l = interleavedStereo[(std::size_t)f * 2];
        const float r = interleavedStereo[(std::size_t)f * 2 + 1];
        if (!std::isfinite(l) || !std::isfinite(r))
            continue;
        const float m = 0.5f * (l + r);
        const std::size_t b = (std::size_t)(f * n / numFrames);
        lvl0.mins[b] = std::min(lvl0.mins[b], m);
        lvl0.maxs[b] = std::max(lvl0.maxs[b], m);
    }
    // Buckets that saw only non-finite samples become silence.
    for (std::size_t i = 0; i < lvl0.mins.size(); ++i) {
        if (lvl0.maxs[i] < lvl0.mins[i]) {
            lvl0.mins[i] = 0.0f;
            lvl0.maxs[i] = 0.0f;
        }
    }
    w.levels.push_back(std::move(lvl0));

    while (w.levels.back().mins.size() > 1) {
        const std::size_t n0 = w.levels.back().mins.size();
        const std::size_t n1 = (n0 + 1) / 2;
        Level next;
        next.mins.resize(n1);
        next.maxs.resize(n1);
        for (std::size_t i = 0; i < n1; ++i) {
            const std::size_t a = i * 2;
            const std::size_t b = a + 1;
            float mn = w.levels.back().mins[a];
            float mx = w.levels.back().maxs[a];
            if (b < n0) { // ceil-halving: last bucket may be unpaired (odd)
                mn = std::min(mn, w.levels.back().mins[b]);
                mx = std::max(mx, w.levels.back().maxs[b]);
            }
            next.mins[i] = mn;
            next.maxs[i] = mx;
        }
        w.levels.push_back(std::move(next));
    }
    return w;
}

// Zoom table shared by the view and tests (W8). Last entry (0.0) means the
// whole track.
inline constexpr double kZoomSeconds[8] = { 4.0, 8.0, 16.0, 32.0,
                                            64.0, 128.0, 256.0, 0.0 };
inline constexpr int kZoomCount = 8;

inline int clampZoomIndex(int idx) {
    return std::clamp(idx, 0, kZoomCount - 1);
}

// Effective window span in seconds for a zoom index over a track of length
// trackLen; the "whole" entry (0.0) and entries >= trackLen resolve to the
// full duration.
inline double zoomSpanSeconds(int idx, double trackLen) {
    if (!(trackLen > 0.0))
        return 0.0;
    const double z = kZoomSeconds[clampZoomIndex(idx)];
    if (z <= 0.0 || z >= trackLen)
        return trackLen;
    return z;
}

// Windowed click -> time mapping (W3): the single source of truth shared by
// paint and mouse seek, so a click lands exactly where the window is drawn.
// The viewport fraction is clamped to [0, 1]; callers clamp to [0, length].
inline double seekTimeFromX(double windowStart, double windowSpan,
                            double x, double viewportWidth) {
    if (!(viewportWidth > 0.0))
        return windowStart;
    const double f = std::clamp(x / viewportWidth, 0.0, 1.0);
    return windowStart + f * windowSpan;
}

} // namespace lean
