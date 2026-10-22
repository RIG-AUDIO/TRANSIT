// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// S4 regression gate: length/frame metadata must be published by Deck::load
// itself, so a track loaded while the transport is stopped reports its
// length immediately — never waiting for the first render() (which only
// happens on Play). This test never renders.
#include "Deck.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* name) {
    std::printf("DECKLOAD_TEST %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        ++failures;
}

std::vector<float> makeStereo(int frames, double rate, double freq) {
    std::vector<float> src((size_t)frames * 2);
    for (int i = 0; i < frames; ++i) {
        const float s = (float)(0.5
            * std::sin(6.283185307179586 * freq * (double)i / rate));
        src[(size_t)i * 2] = s;
        src[(size_t)i * 2 + 1] = s;
    }
    return src;
}

bool close(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

int run() {
    lean::Deck deck;

    // Load a 30 s track while paused.
    constexpr double kRateA = 48000.0;
    const int framesA = (int)(kRateA * 30.0);
    deck.load(makeStereo(framesA, kRateA, 440.0), kRateA);
    check(!deck.isPlaying(), "load leaves the deck paused");
    check(deck.getNumFrames() == framesA, "frames published without render");
    check(close(deck.getLengthSeconds(), 30.0),
          "length available while paused");

    // Swap in a different length while STILL paused (the S4 defect: the
    // old length stuck until the first Play press).
    constexpr double kRateB = 44100.0;
    const int framesB = (int)(kRateB * 7.0);
    deck.load(makeStereo(framesB, kRateB, 220.0), kRateB);
    check(!deck.isPlaying(), "re-load keeps the deck paused");
    check(deck.getNumFrames() == framesB, "frames update after paused load");
    check(close(deck.getLengthSeconds(), 7.0),
          "length updates after paused reload");

    // Invalid rate falls back to 44100 (documented Deck::load contract).
    const int framesC = 44100;
    deck.load(makeStereo(framesC, 1.0, 110.0), 0.0);
    check(close(deck.getLengthSeconds(), 1.0), "zero rate falls back");

    if (failures == 0)
        std::printf("DECKLOAD_TEST PASS\n");
    else
        std::printf("DECKLOAD_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
