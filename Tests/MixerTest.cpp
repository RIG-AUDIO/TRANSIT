// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// M1 mixer gates (plain C++, no JUCE):
//  1. Unity path (flat EQ, fader/trim/master 1, xfade hard to A) nulls
//     against the source below -90 dBFS.
//  2. Hard-right crossfade passes deck B only; deck A leaks nothing.
//  3. Center crossfade is equal-power (-3.01 dB per side).
//  4. EQ kill/boost hits its targets (low -24 dB, high +6 dB).
#include "Mixer.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kRate = 48000.0;

std::vector<float> sineStereo(int frames, double freqL, double freqR, float amp) {
    std::vector<float> buf((size_t)frames * 2);
    for (int i = 0; i < frames; ++i) {
        const double t = (double)i / kRate;
        buf[(size_t)i * 2] = amp * (float)sin(2.0 * 3.141592653589793 * freqL * t);
        buf[(size_t)i * 2 + 1] = amp * (float)sin(2.0 * 3.141592653589793 * freqR * t);
    }
    return buf;
}

void renderAll(lean::Mixer& m, std::vector<float>& L, std::vector<float>& R, int frames) {
    m.snap(); // deterministic: settle all smoothed controls to their targets
    L.assign((size_t)frames, 0.0f);
    R.assign((size_t)frames, 0.0f);
    int done = 0;
    while (done < frames) {
        const int n = std::min(512, frames - done);
        std::vector<float> bl((size_t)n, 0.0f), br((size_t)n, 0.0f);
        m.render(bl.data(), br.data(), n);
        for (int i = 0; i < n; ++i) {
            L[(size_t)done + (size_t)i] = bl[(size_t)i];
            R[(size_t)done + (size_t)i] = br[(size_t)i];
        }
        done += n;
    }
}

double peakDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double peak = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isfinite(a[i]))
            return 1e30;
        peak = std::max(peak, (double)std::abs(a[i] - b[i]));
    }
    return peak;
}

double rms(const std::vector<float>& v) {
    double s = 0.0;
    for (float x : v)
        s += (double)x * x;
    return sqrt(s / (double)v.size());
}

int failures = 0;

void check(bool cond, const char* name, double a = 0.0, double b = 0.0) {
    std::printf("MIXER_TEST %s %s (%.4f vs %.4f)\n", cond ? "ok  " : "FAIL", name, a, b);
    if (!cond)
        ++failures;
}

int run() {
    const int frames = (int)kRate * 2;
    auto srcA = sineStereo(frames, 440.0, 440.0, 0.5f);
    auto srcB = sineStereo(frames, 880.0, 880.0, 0.5f);

    // 1. Unity through channel A.
    {
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.channel(0).deck.load(srcA, kRate);
        m.channel(0).deck.play(true);
        m.setEq(0, 0.0, 0.0, 0.0);
        m.setCrossfader(0.0f);
        std::vector<float> L, R;
        renderAll(m, L, R, frames);
        std::vector<float> refL(frames), refR(frames);
        for (int i = 0; i < frames; ++i) {
            refL[(size_t)i] = srcA[(size_t)i * 2];
            refR[(size_t)i] = srcA[(size_t)i * 2 + 1];
        }
        const double dL = 20.0 * log10(peakDiff(L, refL) + 1e-30);
        const double dR = 20.0 * log10(peakDiff(R, refR) + 1e-30);
        check(dL < -90.0 && dR < -90.0, "unity null dB", dL, dR);
        check(std::abs(m.channelPeak(0) - 0.5) < 0.05, "channel peak",
              m.channelPeak(0), 0.5);
    }

    // 2. Hard right: B only.
    {
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.channel(0).deck.load(srcA, kRate);
        m.channel(1).deck.load(srcB, kRate);
        m.channel(0).deck.play(true);
        m.channel(1).deck.play(true);
        m.setCrossfader(1.0f);
        std::vector<float> L, R;
        renderAll(m, L, R, frames);
        std::vector<float> refL(frames), refR(frames);
        for (int i = 0; i < frames; ++i) {
            refL[(size_t)i] = srcB[(size_t)i * 2];
            refR[(size_t)i] = srcB[(size_t)i * 2 + 1];
        }
        const double dB = 20.0 * log10(peakDiff(L, refL) + 1e-30);
        check(dB < -90.0, "hard-right null dB", dB, -90.0);
        // A-leak: mute B, output must be silent.
        lean::Mixer m2;
        m2.setOutputRate(kRate);
        m2.channel(0).deck.load(srcA, kRate);
        m2.channel(0).deck.play(true);
        m2.setCrossfader(1.0f);
        renderAll(m2, L, R, frames);
        check(rms(L) < 1e-9 && rms(R) < 1e-9, "a-leak rms", rms(L), rms(R));
    }

    // 3. Center is equal power.
    {
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.channel(0).deck.load(srcA, kRate);
        m.channel(0).deck.play(true);
        m.setCrossfader(0.5f);
        std::vector<float> L, R;
        renderAll(m, L, R, frames);
        std::vector<float> refL(frames);
        for (int i = 0; i < frames; ++i)
            refL[(size_t)i] = srcA[(size_t)i * 2];
        const double ratio = rms(L) / rms(refL);
        check(std::abs(ratio - 0.70710678) < 0.005, "center gain", ratio, 0.7071);
    }

    // 4. EQ kill and boost.
    {
        auto low = sineStereo(frames, 100.0, 100.0, 0.5f);
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.channel(0).deck.load(low, kRate);
        m.channel(0).deck.play(true);
        m.setCrossfader(0.0f);
        m.setEq(0, -24.0, 0.0, 0.0);
        std::vector<float> L, R;
        renderAll(m, L, R, frames);
        std::vector<float> refL(frames);
        for (int i = 0; i < frames; ++i)
            refL[(size_t)i] = low[(size_t)i * 2];
        // Skip the first 0.25 s (filter settle), then compare RMS.
        const int skip = (int)kRate / 4;
        std::vector<float> tailL(L.begin() + skip, L.end());
        std::vector<float> tailRef(refL.begin() + skip, refL.end());
        const double att = 20.0 * log10(rms(tailL) / rms(tailRef) + 1e-30);
        check(att < -20.0, "low kill dB", att, -24.0);

        auto hi = sineStereo(frames, 10000.0, 10000.0, 0.4f);
        lean::Mixer m3;
        m3.setOutputRate(kRate);
        m3.channel(0).deck.load(hi, kRate);
        m3.channel(0).deck.play(true);
        m3.setCrossfader(0.0f);
        m3.setEq(0, 0.0, 0.0, 6.0);
        renderAll(m3, L, R, frames);
        std::vector<float> tailH(L.begin() + skip, L.end());
        std::vector<float> refH(frames - skip);
        for (int i = 0; i < frames - skip; ++i)
            refH[(size_t)i] = hi[(size_t)(i + skip) * 2];
        const double boost = rms(tailH) / rms(refH);
        check(std::abs(boost - 2.0) < 0.1, "high boost x", boost, 2.0);
    }

    if (failures == 0)
        std::printf("MIXER_TEST PASS\n");
    else
        std::printf("MIXER_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

struct StubAux : public lean::AuxHost {
    float gain = 0.5f;
    void processAux(int, float* left, float* right, int n) override {
        for (int i = 0; i < n; ++i) {
            left[i] *= gain;
            right[i] *= gain;
        }
    }
};

int runAux() {
    const int frames = (int)kRate * 2;
    auto srcA = sineStereo(frames, 440.0, 440.0, 0.5f);
    std::vector<float> ref(frames);
    for (int i = 0; i < frames; ++i)
        ref[(size_t)i] = srcA[(size_t)i * 2];

    // Aux tap: send 1, return 1, stub gain 0.5 -> master carries 1.5x.
    {
        StubAux stub;
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.setAuxHost(&stub);
        m.channel(0).deck.load(srcA, kRate);
        m.channel(0).deck.play(true);
        m.setCrossfader(0.0f);
        m.setSend(0, 0, 1.0f);
        m.setAuxReturn(0, 1.0f);
        m.snap();
        std::vector<float> L, R;
        renderAll(m, L, R, frames);
        const double ratio = rms(L) / rms(ref);
        check(std::abs(ratio - 1.5) < 0.02, "aux 1.5x", ratio, 1.5);
    }
    // Return closed -> unity (aux silent).
    {
        StubAux stub;
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.setAuxHost(&stub);
        m.channel(0).deck.load(srcA, kRate);
        m.channel(0).deck.play(true);
        m.setCrossfader(0.0f);
        m.setSend(0, 0, 1.0f);
        m.setAuxReturn(0, 0.0f);
        m.snap();
        std::vector<float> L, R;
        renderAll(m, L, R, frames);
        const double d = 20.0 * log10(peakDiff(L, ref) + 1e-30);
        check(d < -90.0, "aux closed null dB", d, -90.0);
    }
    // No host -> sends are inert, unity holds.
    {
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.channel(0).deck.load(srcA, kRate);
        m.channel(0).deck.play(true);
        m.setCrossfader(0.0f);
        m.setSend(0, 0, 1.0f);
        m.snap();
        std::vector<float> L, R;
        renderAll(m, L, R, frames);
        const double d = 20.0 * log10(peakDiff(L, ref) + 1e-30);
        check(d < -90.0, "no-host null dB", d, -90.0);
    }

    if (failures == 0)
        std::printf("AUX_TEST PASS\n");
    else
        std::printf("AUX_TEST FAIL (%d)\n", failures);
    return failures;
}

void renderPhones(lean::Mixer& m, std::vector<float>& P, std::vector<float>& Q,
                  int frames) {
    m.snap(); // deterministic: settle all smoothed controls to their targets
    P.assign((size_t)frames, 0.0f);
    Q.assign((size_t)frames, 0.0f);
    std::vector<float> L((size_t)frames, 0.0f), R((size_t)frames, 0.0f);
    int done = 0;
    while (done < frames) {
        const int n = std::min(512, frames - done);
        std::vector<float> bl((size_t)n, 0.0f), br((size_t)n, 0.0f), pl((size_t)n, 0.0f), pq((size_t)n, 0.0f);
        m.render(bl.data(), br.data(), pl.data(), pq.data(), n);
        for (int i = 0; i < n; ++i) {
            P[(size_t)done + (size_t)i] = pl[(size_t)i];
            Q[(size_t)done + (size_t)i] = pq[(size_t)i];
        }
        done += n;
    }
}

int runCue() {
    const int frames = (int)kRate * 2;
    auto srcA = sineStereo(frames, 440.0, 440.0, 0.5f);
    auto srcB = sineStereo(frames, 880.0, 880.0, 0.5f);
    std::vector<float> refA(frames), refB(frames);
    for (int i = 0; i < frames; ++i) {
        refA[(size_t)i] = srcA[(size_t)i * 2];
        refB[(size_t)i] = srcB[(size_t)i * 2];
    }

    // 5. Pure cue: fader A down, cue A on -> master silent, phones carry A.
    {
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.channel(0).deck.load(srcA, kRate);
        m.channel(0).deck.play(true);
        m.setFader(0, 0.0f);
        m.setCue(0, true);
        m.setCueBlend(1.0f);
        m.setCrossfader(0.0f);
        std::vector<float> L, R, P, Q;
        renderAll(m, L, R, frames);
        check(rms(L) < 1e-9, "cue master silent", rms(L), 0.0);
        lean::Mixer m2;
        m2.setOutputRate(kRate);
        m2.channel(0).deck.load(srcA, kRate);
        m2.channel(0).deck.play(true);
        m2.setFader(0, 0.0f);
        m2.setCue(0, true);
        m2.setCueBlend(1.0f);
        m2.setCrossfader(0.0f);
        renderPhones(m2, P, Q, frames);
        const double d = 20.0 * log10(peakDiff(P, refA) + 1e-30);
        check(d < -90.0, "cue phones null dB", d, -90.0);
    }

    // 6. Blend 0 = pure master in the phones.
    {
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.channel(0).deck.load(srcA, kRate);
        m.channel(1).deck.load(srcB, kRate);
        m.channel(0).deck.play(true);
        m.channel(1).deck.play(true);
        m.setCue(1, true);
        m.setCueBlend(0.0f);
        m.setCrossfader(0.0f); // master = A only
        std::vector<float> P, Q;
        renderPhones(m, P, Q, frames);
        const double d = 20.0 * log10(peakDiff(P, refA) + 1e-30);
        check(d < -90.0, "blend-0 phones=master dB", d, -90.0);
    }

    if (failures == 0)
        std::printf("CUE_TEST PASS\n");
    else
        std::printf("CUE_TEST FAIL (%d)\n", failures);
    return failures;
}

} // namespace

int main() {
    const int mixerResult = run();
    failures = 0;
    const int cueResult = runCue();
    failures = 0;
    const int auxResult = runAux();
    return (mixerResult == 0 && cueResult == 0 && auxResult == 0) ? 0 : 1;
}
