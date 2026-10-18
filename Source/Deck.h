// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

// MIT-licensed time-stretch backend (see README). Plain C++, no JUCE.
// Warning quarantine: third-party headers are exempt from our /WX rule;
// warnings inside them are not ours to fix.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <signalsmith-stretch/signalsmith-stretch.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "Smooth.h"

namespace lean {

// Immutable audio buffer shared with the audio thread by shared_ptr. The
// message thread stages a new buffer under a staging mutex and raises a
// flag; the audio thread picks it up at the next block boundary with
// try_lock (never blocks). Steady-state render never allocates, locks, or
// touches the message thread.
struct DeckBuffer {
    std::vector<float> stereo; // interleaved LRLR...
    int frames = 0;
    double rate = 44100.0;
};

// One-shot stereo file player: tempo (resample) + gain + optional keylock.
//
// Thread model: load/setOutputRate/setKeylock/setCuePoint are message-thread
// only and publish through atomics plus the staged buffer. render() runs on
// the audio thread (or the test thread) and owns the stretcher, the EQ-free
// signal path state, and the active buffer reference.
class Deck {
public:
    Deck();

    // Stages a new buffer for pickup at the next block boundary. Resets the
    // playhead and cue point. Message thread only.
    void load(std::vector<float> interleavedStereo, double sampleRate);

    void setOutputRate(double rate);
    void setTempo(double t);
    double getTempo() const { return tempo.load(std::memory_order_relaxed); }

    // Sync helper: tempo that matches leaderEffectiveBpm given our analyzed
    // BPM, or -1 when our BPM is unknown. Pure math, unit-tested.
    static double syncTempoFor(double ownBpm, double leaderEffectiveBpm) {
        if (ownBpm <= 0.0 || leaderEffectiveBpm <= 0.0)
            return -1.0;
        const double t = leaderEffectiveBpm / ownBpm;
        return t < 0.25 ? 0.25 : (t > 4.0 ? 4.0 : t);
    }
    void setGain(float g) { gain.setTarget(g); }
    float gainTarget() const { return gain.targetValue(); }
    void setKeylock(bool enabled);
    bool isKeylock() const { return keylock.load(std::memory_order_relaxed); }
    void setKeylockEco(bool eco);
    void play(bool shouldPlay) { playing.store(shouldPlay); }
    void setPositionSeconds(double seconds);
    void setCuePointSeconds(double seconds);
    double getCuePointSeconds() const;
    bool isPlaying() const { return playing.load(std::memory_order_relaxed); }
    double getPositionSeconds() const;
    double getLengthSeconds() const;
    int getNumFrames() const { return infoFrames.load(std::memory_order_relaxed); }
    // Stretcher latency in output samples (0 when keylock is off).
    int getStretchLatency() const;

    // Adds (mixes) the deck output into left/right. Output buffers must hold
    // at least numSamples floats each.
    void render(float* left, float* right, int numSamples);

private:
    static float cubic(float y0, float y1, float y2, float y3, float t);
    static float frame(const DeckBuffer& buf, int index, int channel);
    void adoptStaged();
    void renderResample(const DeckBuffer& buf, float* left, float* right,
                        int numSamples, double step);
    void renderStretch(const DeckBuffer& buf, float* left, float* right,
                       int numSamples);

    std::shared_ptr<const DeckBuffer> active; // audio thread only
    std::shared_ptr<DeckBuffer> staged;       // guarded by stageMutex
    std::mutex stageMutex;
    std::atomic<bool> stagedReady{ false };

    std::atomic<double> fileRate{ 44100.0 };
    std::atomic<double> outputRate{ 44100.0 };
    std::atomic<double> tempo{ 1.0 };
    std::atomic<double> position{ 0.0 }; // fractional frame, file domain
    SmoothFloat gain{ 1.0f };
    std::atomic<bool> playing{ false };
    double cueFrame = 0.0; // message thread only

    std::atomic<int> infoFrames{ 0 };
    std::atomic<double> infoRate{ 44100.0 };

    signalsmith::stretch::SignalsmithStretch<float> stretch; // audio only
    std::atomic<bool> keylock{ false };
    std::atomic<bool> eco{ false }; // cheaper preset: less CPU, less quality
    std::atomic<bool> stretchReady{ false };
    std::atomic<bool> stretchDirty{ true };
    std::atomic<int> stretchLatency{ 0 };
    double stretchCarry = 0.0; // audio only
    int tailOut = 0;           // audio only
};

} // namespace lean
