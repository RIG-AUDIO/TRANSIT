// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <cmath>

namespace lean {

// Lock-free smoothed parameter. The message thread sets targets; the audio
// thread pulls per-sample values through a one-pole follower (~40 Hz), which
// also eliminates zipper noise on fader/EQ-adjacent moves. Cross-thread
// traffic is a single atomic load per sample; `cur` is audio-thread only.
class SmoothFloat {
public:
    SmoothFloat() = default;
    explicit SmoothFloat(float v) : target(v), cur(v) {}

    void setTarget(float v) { target.store(v, std::memory_order_relaxed); }
    void snap(float v) {
        target.store(v, std::memory_order_relaxed);
        cur = v;
    }
    float next(float coeff) {
        const float t = target.load(std::memory_order_relaxed);
        cur += (t - cur) * coeff;
        if (std::abs(t - cur) < 1e-6f)
            cur = t;
        return cur;
    }
    float current() const { return cur; }
    // Last commanded value; the race-free read for other threads.
    float targetValue() const { return target.load(std::memory_order_relaxed); }

private:
    std::atomic<float> target{ 0.0f };
    float cur = 0.0f; // audio thread only
};

// Compile-time proof of the lock-free claim. If a platform ever fails this,
// the realtime design must be revisited rather than silently degrading.
static_assert(std::atomic<float>::is_always_lock_free,
              "atomic<float> must be lock-free");
static_assert(std::atomic<double>::is_always_lock_free,
              "atomic<double> must be lock-free");
static_assert(std::atomic<bool>::is_always_lock_free,
              "atomic<bool> must be lock-free");
static_assert(std::atomic<int>::is_always_lock_free,
              "atomic<int> must be lock-free");

// One-pole coefficient for ~40 Hz smoothing at the given sample rate.
inline float smoothCoeff(double sampleRate) {
    return 1.0f
        - (float)exp(-2.0 * 3.141592653589793 * 40.0
                     / (sampleRate > 0.0 ? sampleRate : 44100.0));
}

} // namespace lean
