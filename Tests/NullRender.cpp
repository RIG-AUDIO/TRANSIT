// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// M0 acceptance gate: unity playback (tempo 1.0, gain 1.0, matched rates)
// must null against the source buffer to better than -90 dBFS peak.
// Also smoke-tests tempo 1.5 for completion without NaN/Inf.
//
// Builds with plain C++ only: no JUCE, no third-party includes.
#include "Deck.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr int kSeconds = 3;
constexpr double kGateDb = -90.0;

bool isFinite(float v) {
    return std::isfinite(v);
}

int run() {
    const int frames = (int)(kRate * kSeconds);
    std::vector<float> src((size_t)frames * 2);
    for (int i = 0; i < frames; ++i) {
        const double t = (double)i / kRate;
        // Two sines plus a transient click train: exercises the interpolator
        // on smooth and discontinuous content.
        float s = (float)(0.4 * sin(2.0 * 3.141592653589793 * 440.0 * t)
            + 0.25 * sin(2.0 * 3.141592653589793 * 880.0 * t));
        if (i % 4800 == 0)
            s += 0.5f;
        src[(size_t)i * 2] = s;
        src[(size_t)i * 2 + 1] = -s;
    }

    // Unity pass.
    lean::Deck deck;
    deck.load(src, kRate);
    deck.setOutputRate(kRate);
    deck.setTempo(1.0);
    deck.setGain(1.0f);
    deck.play(true);

    std::vector<float> left(frames, 0.0f), right(frames, 0.0f);
    int done = 0;
    while (done < frames && deck.isPlaying()) {
        const int n = done + 512 <= frames ? 512 : frames - done;
        std::vector<float> bl((size_t)n, 0.0f), br((size_t)n, 0.0f);
        deck.render(bl.data(), br.data(), n);
        for (int i = 0; i < n; ++i) {
            left[(size_t)done + (size_t)i] = bl[(size_t)i];
            right[(size_t)done + (size_t)i] = br[(size_t)i];
        }
        done += n;
    }

    double peak = 0.0;
    for (int i = 0; i < frames; ++i) {
        const size_t fi = (size_t)i;
        if (!isFinite(left[fi]) || !isFinite(right[fi])) {
            std::printf("NULL_RENDER FAIL: non-finite sample at %d\n", i);
            return 1;
        }
        peak = std::max(peak, (double)std::abs(left[fi] - src[fi * 2]));
        peak = std::max(peak, (double)std::abs(right[fi] - src[fi * 2 + 1]));
    }
    const double peakDb = 20.0 * log10(peak + 1e-30);
    std::printf("NULL_RENDER unity peak diff: %.1f dBFS (gate %.0f)\n", peakDb, kGateDb);
    if (peakDb > kGateDb) {
        std::printf("NULL_RENDER FAIL: above gate\n");
        return 1;
    }

    // Tempo smoke pass.
    lean::Deck fast;
    fast.load(src, kRate);
    fast.setOutputRate(kRate);
    fast.setTempo(1.5);
    fast.setGain(1.0f);
    fast.play(true);
    std::vector<float> bl(512, 0.0f), br(512, 0.0f);
    double outPeak = 0.0;
    int blocks = 0;
    while (fast.isPlaying() && blocks < 10000) {
        std::fill(bl.begin(), bl.end(), 0.0f);
        std::fill(br.begin(), br.end(), 0.0f);
        fast.render(bl.data(), br.data(), 512);
        for (int i = 0; i < 512; ++i) {
            const size_t bi = (size_t)i;
            if (!isFinite(bl[bi]) || !isFinite(br[bi])) {
                std::printf("NULL_RENDER FAIL: non-finite at tempo 1.5\n");
                return 1;
            }
            outPeak = std::max(outPeak, (double)std::abs(bl[bi]));
        }
        ++blocks;
    }
    std::printf("NULL_RENDER tempo-1.5 pass: %d blocks, peak %.3f\n", blocks, outPeak);
    if (fast.isPlaying()) {
        std::printf("NULL_RENDER FAIL: deck did not finish\n");
        return 1;
    }
    std::printf("NULL_RENDER PASS\n");
    return 0;
}

} // namespace

int main() {
    return run();
}
