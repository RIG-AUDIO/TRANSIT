// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// M2 keylock gates (plain C++, no JUCE):
//  A. Keylock ON at tempo 1.5: output pitch stays at the source pitch.
//  B. Keylock OFF at tempo 1.5: output pitch rises with tempo (resample).
//  C. Keylock ON at tempo 1.0: pitch and level match the source.
// Pitch is measured by zero-crossing rate over the stable middle region.
#include "Deck.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr double kFreq = 440.0;

std::vector<float> sineStereo(int frames, double freq, float amp) {
    std::vector<float> buf((size_t)frames * 2);
    for (int i = 0; i < frames; ++i) {
        const double t = (double)i / kRate;
        const float s = amp * (float)sin(2.0 * 3.141592653589793 * freq * t);
        buf[(size_t)i * 2] = s;
        buf[(size_t)i * 2 + 1] = s;
    }
    return buf;
}

std::vector<float> renderAll(lean::Deck& deck, size_t maxFrames) {
    std::vector<float> out;
    out.reserve(maxFrames);
    int guard = 0;
    while (deck.isPlaying() && out.size() < maxFrames && guard < 10000) {
        float bl[512], br[512];
        for (int i = 0; i < 512; ++i)
            bl[i] = br[i] = 0.0f;
        deck.render(bl, br, 512);
        for (int i = 0; i < 512; ++i) {
            if (!std::isfinite(bl[i]))
                return { (float)1e30f };
            out.push_back(bl[i]);
        }
        ++guard;
    }
    return out;
}

double crossingFreq(const std::vector<float>& v) {
    const size_t from = v.size() / 4;
    const size_t to = v.size() * 3 / 4;
    long crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if ((v[i - 1] <= 0.0f && v[i] > 0.0f) || (v[i - 1] >= 0.0f && v[i] < 0.0f))
            ++crossings;
    return (double)crossings * kRate / (2.0 * (double)(to - from));
}

double rms(const std::vector<float>& v) {
    const size_t from = v.size() / 4;
    const size_t to = v.size() * 3 / 4;
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += (double)v[i] * v[i];
    return sqrt(s / (double)(to - from));
}

int failures = 0;

void check(bool cond, const char* name, double got, double want) {
    std::printf("KEYLOCK_TEST %s %s (got %.2f want %.2f)\n",
                cond ? "ok  " : "FAIL", name, got, want);
    if (!cond)
        ++failures;
}

int run() {
    const size_t frames = (size_t)kRate * 4;
    auto src = sineStereo((int)frames, kFreq, 0.5f);

    // A. Keylock holds pitch at tempo 1.5.
    {
        lean::Deck deck;
        deck.load(src, kRate);
        deck.setOutputRate(kRate);
        deck.setKeylock(true);
        deck.setTempo(1.5);
        deck.play(true);
        std::printf("KEYLOCK_TEST stretcher latency: %d samples\n",
                    deck.getStretchLatency());
        auto out = renderAll(deck, frames * 2);
        const double f = crossingFreq(out);
        check(std::abs(f - kFreq) / kFreq < 0.03, "locked pitch", f, kFreq);
        const double durRatio = (double)out.size() / (double)frames;
        check(std::abs(durRatio - 1.0 / 1.5) < 0.08, "locked duration ratio",
              durRatio, 1.0 / 1.5);
    }

    // B. Resample (no keylock) shifts pitch with tempo.
    {
        lean::Deck deck;
        deck.load(src, kRate);
        deck.setOutputRate(kRate);
        deck.setKeylock(false);
        deck.setTempo(1.5);
        deck.play(true);
        auto out = renderAll(deck, frames);
        const double f = crossingFreq(out);
        check(std::abs(f - kFreq * 1.5) / (kFreq * 1.5) < 0.03, "shifted pitch",
              f, kFreq * 1.5);
    }

    // C. Keylock at unity preserves pitch and level.
    {
        lean::Deck deck;
        deck.load(src, kRate);
        deck.setOutputRate(kRate);
        deck.setKeylock(true);
        deck.setTempo(1.0);
        deck.play(true);
        auto out = renderAll(deck, frames * 2);
        const double f = crossingFreq(out);
        check(std::abs(f - kFreq) / kFreq < 0.03, "unity pitch", f, kFreq);
        std::vector<float> ref(src.size() / 2, 0.0f);
        for (size_t i = 0; i < ref.size(); ++i)
            ref[i] = src[i * 2];
        const double ratio = rms(out) / rms(ref);
        check(std::abs(ratio - 1.0) < 0.05, "unity level", ratio, 1.0);
    }

    // D. Eco preset holds pitch too (cheaper path, looser gate).
    {
        lean::Deck deck;
        deck.load(src, kRate);
        deck.setOutputRate(kRate);
        deck.setKeylockEco(true);
        deck.setKeylock(true);
        deck.setTempo(1.25);
        deck.play(true);
        auto out = renderAll(deck, frames * 2);
        const double f = crossingFreq(out);
        check(std::abs(f - kFreq) / kFreq < 0.05, "eco pitch", f, kFreq);
    }

    if (failures == 0)
        std::printf("KEYLOCK_TEST PASS\n");
    else
        std::printf("KEYLOCK_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
