// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Waveform 2.0 gates (W6): signed min/max pyramid correctness, multires
// downsampling boundaries, LOD level selection, mono mix-down semantics,
// absurd input rejection, non-finite exclusion, the W3 window->seek formula
// and the W8 zoom table.
#include "Waveform.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* name, double got, double want) {
    std::printf("WAVEFORM_TEST %s %s (got %.6f want %.6f)\n",
                cond ? "ok  " : "FAIL", name, got, want);
    if (!cond)
        ++failures;
}

// Ramp signal in [-1, 1]: value at frame f of n frames (mono mix of L=R).
float rampValue(std::int64_t f, std::int64_t n) {
    return (float)((double)f / (double)(n - 1) * 2.0 - 1.0);
}

std::vector<float> stereoEqual(const std::vector<float>& mono) {
    std::vector<float> out(mono.size() * 2);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        out[i * 2] = mono[i];
        out[i * 2 + 1] = mono[i];
    }
    return out;
}

void named(const char* tag, const char* what, char* buf, std::size_t n) {
    std::snprintf(buf, n, "%s: %s", tag, what);
}

// Pyramid structural invariants: sizes halve (ceil) down to one bucket,
// no level ever exceeds the level-0 global min/max (exact propagation),
// and each parent is the exact pairwise min/max of its children.
void checkPyramidShape(const lean::Waveform& w, const char* tag) {
    char name[96];
    bool chainOk = !w.levels.empty();
    for (std::size_t k = 1; k < w.levels.size(); ++k) {
        const std::size_t want = (w.levels[k - 1].mins.size() + 1) / 2;
        if (w.levels[k].mins.size() != want
            || w.levels[k].mins.size() != w.levels[k].maxs.size())
            chainOk = false;
    }
    if (chainOk && (w.levels.empty() || w.levels.back().mins.size() != 1))
        chainOk = false;
    named(tag, "size chain halves to one", name, sizeof(name));
    check(chainOk, name, (double)chainOk, 1.0);

    bool globalOk = true;
    if (!w.levels.empty() && !w.levels[0].mins.empty()) {
        const float gMin = w.levels[0].mins[0];
        const float gMax = w.levels[0].maxs[0];
        for (const auto& lv : w.levels) {
            for (std::size_t i = 0; i < lv.mins.size(); ++i) {
                if (lv.mins[i] < gMin || lv.maxs[i] > gMax
                    || lv.maxs[i] < gMin || lv.mins[i] > gMax)
                    globalOk = false;
            }
        }
    } else {
        globalOk = false;
    }
    named(tag, "global range propagates", name, sizeof(name));
    check(globalOk, name, (double)globalOk, 1.0);

    bool pairOk = true;
    if (w.levels.size() > 1) {
        const auto& a = w.levels[0];
        const auto& b = w.levels[1];
        for (std::size_t i = 0; i < b.mins.size(); ++i) {
            const std::size_t x = i * 2;
            float mn = a.mins[x];
            float mx = a.maxs[x];
            if (x + 1 < a.mins.size()) {
                mn = std::min(mn, a.mins[x + 1]);
                mx = std::max(mx, a.maxs[x + 1]);
            }
            if (std::fabs(mn - b.mins[i]) > 1e-9f
                || std::fabs(mx - b.maxs[i]) > 1e-9f)
                pairOk = false;
        }
    }
    named(tag, "pairwise downsample exact", name, sizeof(name));
    check(pairOk, name, (double)pairOk, 1.0);
}

int run() {
    constexpr double rate = 48000.0;
    constexpr int frames = (int)rate * 4; // 4 s

    // --- Tone: mono mix-down = 0.75 * sin, sine amplitude bounds,
    //     duration/rate/frame consistency (payload side). ---
    {
        std::vector<float> tone((size_t)frames * 2);
        for (int i = 0; i < frames; ++i) {
            const float s =
                0.5f * (float)sin(2.0 * 3.141592653589793 * 440.0 * i / rate);
            tone[(size_t)i * 2] = s;
            tone[(size_t)i * 2 + 1] = s * 0.5f;
        }
        auto w = lean::buildWaveform(tone.data(), frames, rate);
        check(w.valid(), "tone: valid", (double)w.valid(), 1.0);
        check(std::fabs(w.durationSeconds - 4.0) < 1e-9, "tone: duration",
              w.durationSeconds, 4.0);
        check(w.frames == frames, "tone: frame count", (double)w.frames,
              (double)frames);
        check(std::fabs(w.sampleRate - rate) < 1e-9, "tone: sample rate",
              w.sampleRate, rate);
        checkPyramidShape(w, "tone");
        float mn = 0.0f, mx = 0.0f;
        w.aggregate(0.0, w.durationSeconds, mn, mx);
        check(mx > 0.745f && mx <= 0.7501f, "tone: sine upper bound",
              (double)mx, 0.75);
        check(mn < -0.745f && mn >= -0.7501f, "tone: sine lower bound",
              (double)mn, -0.75);
    }

    // --- Silence: exact zeros everywhere. ---
    {
        std::vector<float> silence((size_t)frames * 2, 0.0f);
        auto w = lean::buildWaveform(silence.data(), frames, rate);
        check(w.valid(), "silence: valid", (double)w.valid(), 1.0);
        float mn = 1.0f, mx = -1.0f;
        w.aggregate(0.0, w.durationSeconds, mn, mx);
        check(std::fabs(mn) < 1e-12 && std::fabs(mx) < 1e-12,
              "silence: zeros", (double)mx, 0.0);
    }

    // --- Empty / zero-length / invalid rates: rejected, aggregate zero. ---
    {
        std::vector<float> tiny(64, 0.25f);
        auto e = lean::buildWaveform(nullptr, 0, rate);
        check(!e.valid(), "null data rejected", (double)e.valid(), 0.0);
        float mn = 1.0f, mx = -1.0f;
        e.aggregate(0.0, 1.0, mn, mx);
        check(std::fabs(mn) < 1e-12 && std::fabs(mx) < 1e-12,
              "invalid: aggregate is zero", (double)mx, 0.0);
        check(!lean::buildWaveform(tiny.data(), 0, rate).valid(),
              "zero frames rejected", 0.0, 0.0);
        check(!lean::buildWaveform(tiny.data(), 16, 0.0).valid(),
              "zero rate rejected", 0.0, 0.0);
        check(!lean::buildWaveform(tiny.data(), 16, -1.0).valid(),
              "negative rate rejected", 0.0, 0.0);
        check(!lean::buildWaveform(tiny.data(), 16, std::nan("")).valid(),
              "non-finite rate rejected", 0.0, 0.0);
    }

    // --- Impulse: a single full-scale spike is visible in its own bucket
    //     and absent from a distant one. ---
    {
        const std::int64_t n = 48000; // 1 s -> level 0 has 2000 buckets
        std::vector<float> buf((size_t)n * 2, 0.0f);
        buf[(size_t)1000 * 2] = 1.0f;
        buf[(size_t)1000 * 2 + 1] = 1.0f;
        auto w = lean::buildWaveform(buf.data(), n, rate);
        check(w.valid(), "impulse: valid", (double)w.valid(), 1.0);
        const double bd = w.bucketSeconds(0);
        check(std::fabs(bd - 0.0005) < 1e-12, "impulse: bucket seconds", bd,
              0.0005);
        const std::int64_t bucket = 1000 * 2000 / n;
        float mn = 0.0f, mx = 0.0f;
        w.aggregate((double)bucket * bd, (double)(bucket + 1) * bd, mn, mx);
        check(std::fabs(mx - 1.0f) < 1e-9f, "impulse: spike in own bucket",
              (double)mx, 1.0);
        const std::int64_t far = (bucket + 1000) % 2000;
        w.aggregate((double)far * bd, (double)(far + 1) * bd, mn, mx);
        check(std::fabs(mx) < 1e-9f && std::fabs(mn) < 1e-9f,
              "impulse: quiet far bucket", (double)mx, 0.0);
    }

    // --- Mono mix-down semantics (W1): out-of-phase content cancels,
    //     identical channels pass through. ---
    {
        const std::int64_t n = 480; // one bucket per frame
        {
            std::vector<float> buf((size_t)n * 2, 1.0f); // L=+1, R=+1
            auto w = lean::buildWaveform(buf.data(), n, rate);
            float mn = 0.0f, mx = 0.0f;
            w.aggregate(0.0, w.durationSeconds, mn, mx);
            check(std::fabs(mx - 1.0f) < 1e-6f && std::fabs(mn - 1.0f) < 1e-6f,
                  "mixdown: equal full-scale", (double)mx, 1.0);
        }
        {
            std::vector<float> buf((size_t)n * 2);
            for (std::size_t i = 0; i < (size_t)n; ++i) {
                buf[i * 2] = 1.0f;
                buf[i * 2 + 1] = -1.0f;
            }
            auto w = lean::buildWaveform(buf.data(), n, rate);
            float mn = 1.0f, mx = -1.0f;
            w.aggregate(0.0, w.durationSeconds, mn, mx);
            check(std::fabs(mx) < 1e-6f && std::fabs(mn) < 1e-6f,
                  "mixdown: out-of-phase cancels", (double)mx, 0.0);
        }
        {
            std::vector<float> buf((size_t)n * 2);
            for (std::size_t i = 0; i < (size_t)n; ++i) {
                buf[i * 2] = 1.0f;
                buf[i * 2 + 1] = 0.0f;
            }
            auto w = lean::buildWaveform(buf.data(), n, rate);
            float mn = 0.0f, mx = 0.0f;
            w.aggregate(0.0, w.durationSeconds, mn, mx);
            check(std::fabs(mx - 0.5f) < 1e-6f, "mixdown: half-scale",
                  (double)mx, 0.5);
        }
    }

    // --- Stereo channel difference: L = -R cancels to near silence. ---
    {
        std::vector<float> pos((size_t)frames);
        std::vector<float> neg((size_t)frames);
        for (int i = 0; i < frames; ++i) {
            const float s =
                0.8f * (float)sin(2.0 * 3.141592653589793 * 220.0 * i / rate);
            pos[(size_t)i] = s;
            neg[(size_t)i] = -s;
        }
        std::vector<float> buf((size_t)frames * 2);
        for (int i = 0; i < frames; ++i) {
            buf[(size_t)i * 2] = pos[(size_t)i];
            buf[(size_t)i * 2 + 1] = neg[(size_t)i];
        }
        auto w = lean::buildWaveform(buf.data(), frames, rate);
        float mn = 1.0f, mx = -1.0f;
        w.aggregate(0.0, w.durationSeconds, mn, mx);
        check(std::fabs(mx) < 1e-5f && std::fabs(mn) < 1e-5f,
              "stereo difference cancels", (double)mx, 0.0);
    }

    // --- Short inputs: 10 frames and a single frame stay valid. ---
    {
        std::vector<float> ten = { 1.0f,  1.0f, -1.0f, -1.0f, 1.0f,
                                   1.0f, -1.0f, -1.0f, 1.0f,  0.5f };
        auto stereo = stereoEqual(ten);
        auto w = lean::buildWaveform(stereo.data(), 10, rate);
        check(w.valid(), "short: 10 frames valid", (double)w.valid(), 1.0);
        checkPyramidShape(w, "short");
        float mn = 0.0f, mx = 0.0f;
        w.aggregate(0.0, w.durationSeconds, mn, mx);
        check(std::fabs(mx - 1.0f) < 1e-6f && std::fabs(mn + 1.0f) < 1e-6f,
              "short: extremes preserved", (double)mx, 1.0);

        std::vector<float> one = { 0.75f };
        auto stereoOne = stereoEqual(one);
        auto w1 = lean::buildWaveform(stereoOne.data(), 1, rate);
        check(w1.valid(), "short: one frame valid", (double)w1.valid(), 1.0);
        checkPyramidShape(w1, "one");
        float mn1 = 0.0f, mx1 = 0.0f;
        w1.aggregate(0.0, w1.durationSeconds, mn1, mx1);
        check(std::fabs(mx1 - 0.75f) < 1e-6f && std::fabs(mn1 - 0.75f) < 1e-6f,
              "short: one frame value", (double)mx1, 0.75);
    }

    // --- Huge frame counts (W6): absurd durations are rejected before any
    //     data is read (the historic int-wrap class of bug). ---
    {
        std::vector<float> tiny(64, 0.0f);
        const std::int64_t huge = (std::int64_t)(rate * 86400.0) + 1;
        auto w = lean::buildWaveform(tiny.data(), huge, rate);
        check(!w.valid(), "huge: >24 h frames rejected", (double)w.valid(), 0.0);
        check(w.frames == 0, "huge: rejected payload is empty",
              (double)w.frames, 0.0);
    }

    // --- Pyramid odd-size boundary: 999 frames -> level 0 has 999 (odd)
    //     buckets; ceil-halving must cover the unpaired tail. ---
    {
        std::int64_t n = 999;
        std::vector<float> mono((size_t)n);
        for (std::int64_t f = 0; f < n; ++f)
            mono[(size_t)f] = rampValue(f, n);
        auto stereo = stereoEqual(mono);
        auto w = lean::buildWaveform(stereo.data(), n, rate);
        check(w.valid(), "odd: valid", (double)w.valid(), 1.0);
        check(w.levels[0].mins.size() == 999, "odd: level 0 has 999 buckets",
              (double)w.levels[0].mins.size(), 999.0);
        checkPyramidShape(w, "odd");
        float mn = 0.0f, mx = 0.0f;
        w.aggregate(0.0, w.durationSeconds, mn, mx);
        check(std::fabs(mx - 1.0f) < 1e-5f && std::fabs(mn + 1.0f) < 1e-5f,
              "odd: ramp extremes", (double)mx, 1.0);
    }

    // --- LOD level selection (W2): a sub-bucket window falls back to the
    //     finest level; a wider window selects a coarser level and therefore
    //     spans more source frames. ---
    {
        const std::int64_t n = 48000; // 1 s -> level 0 = 2000 buckets
        std::vector<float> mono((size_t)n);
        for (std::int64_t f = 0; f < n; ++f)
            mono[(size_t)f] = rampValue(f, n);
        auto stereo = stereoEqual(mono);
        auto w = lean::buildWaveform(stereo.data(), n, rate);
        check(w.valid(), "lod: valid", (double)w.valid(), 1.0);
        const double bd = w.bucketSeconds(0); // 0.0005 s, 24 frames/bucket

        // Window narrower than one level-0 bucket: fallback to level 0,
        // bucket 0 = frames [0, 24).
        float mn = 0.0f, mx = 0.0f;
        w.aggregate(0.0, 0.0004, mn, mx);
        const double wantFine = rampValue(23, n);
        check(std::fabs(mx - wantFine) < 1e-4, "lod: finest level fallback",
              (double)mx, wantFine);

        // Window of 3 level-0 buckets: coarsest bucket <= 0.0015 s is the
        // 1000-bucket level (1 ms); two of its buckets cover frames [0, 96)
        // -> max = ramp(95), NOT the level-0 ramp(71).
        w.aggregate(0.0, 3.0 * bd, mn, mx);
        const double wantCoarse = rampValue(95, n);
        check(std::fabs(mx - wantCoarse) < 1e-4, "lod: coarser level selected",
              (double)mx, wantCoarse);
    }

    // --- W3: windowed click -> time mapping, viewport edges and clamps. ---
    {
        const double s = 100.0, span = 40.0, W = 800.0;
        check(std::fabs(lean::seekTimeFromX(s, span, 0.0, W) - s) < 1e-9,
              "seek: x=0 is window start", lean::seekTimeFromX(s, span, 0.0, W), s);
        check(std::fabs(lean::seekTimeFromX(s, span, W, W) - (s + span)) < 1e-9,
              "seek: x=width is window end", lean::seekTimeFromX(s, span, W, W),
              s + span);
        check(std::fabs(lean::seekTimeFromX(s, span, 400.0, W) - 120.0) < 1e-9,
              "seek: midpoint", lean::seekTimeFromX(s, span, 400.0, W), 120.0);
        check(std::fabs(lean::seekTimeFromX(s, span, -50.0, W) - s) < 1e-9,
              "seek: negative x clamps", lean::seekTimeFromX(s, span, -50.0, W),
              s);
        check(std::fabs(lean::seekTimeFromX(s, span, 850.0, W) - (s + span)) < 1e-9,
              "seek: overshoot clamps", lean::seekTimeFromX(s, span, 850.0, W),
              s + span);
        check(std::fabs(lean::seekTimeFromX(s, span, 50.0, 0.0) - s) < 1e-9,
              "seek: zero width is safe", lean::seekTimeFromX(s, span, 50.0, 0.0),
              s);
    }

    // --- Aggregate edges: zero-width window, out-of-range window, swap. ---
    {
        const std::int64_t n = 48000;
        std::vector<float> mono((size_t)n);
        for (std::int64_t f = 0; f < n; ++f)
            mono[(size_t)f] = rampValue(f, n);
        auto stereo = stereoEqual(mono);
        auto w = lean::buildWaveform(stereo.data(), n, rate);
        const double bd = w.bucketSeconds(0);
        const std::int64_t bucket = 41; // impulse bucket pattern reused here
        float mn = 0.0f, mx = 0.0f;
        const double mid = (double)bucket * bd + bd * 0.5;
        w.aggregate(mid, mid, mn, mx); // zero width -> containing bucket
        const double bucketMax = rampValue((bucket + 1) * 24 - 1, n);
        check(std::fabs(mx - bucketMax) < 1e-4, "edge: zero-width window",
              (double)mx, bucketMax);

        float mnA = 0.0f, mxA = 0.0f, mnB = 0.0f, mxB = 0.0f;
        w.aggregate(-5.0, 99.0, mnA, mxA); // clamped to whole track
        w.aggregate(0.0, w.durationSeconds, mnB, mxB);
        check(std::fabs(mnA - mnB) < 1e-12f && std::fabs(mxA - mxB) < 1e-12f,
              "edge: out-of-range clamps", (double)mxA, (double)mxB);
        w.aggregate(0.6, 0.2, mnA, mxA); // reversed -> swapped
        w.aggregate(0.2, 0.6, mnB, mxB);
        check(std::fabs(mnA - mnB) < 1e-12f && std::fabs(mxA - mxB) < 1e-12f,
              "edge: reversed window swaps", (double)mxA, (double)mxB);
    }

    // --- Non-finite input: excluded from the payload, never propagated. ---
    {
        const std::int64_t n = 480; // one bucket per frame
        const float nan = std::nan("");
        std::vector<float> allNan((size_t)n * 2);
        for (std::size_t i = 0; i < (size_t)n; ++i) {
            allNan[i * 2] = nan;
            allNan[i * 2 + 1] = nan;
        }
        auto w = lean::buildWaveform(allNan.data(), n, rate);
        check(w.valid(), "nonfinite: payload still valid", (double)w.valid(), 1.0);
        float mn = 1.0f, mx = -1.0f;
        w.aggregate(0.0, w.durationSeconds, mn, mx);
        check(std::fabs(mn) < 1e-12 && std::fabs(mx) < 1e-12,
              "nonfinite: all-NaN reads as silence", (double)mx, 0.0);

        std::vector<float> mixed((size_t)n * 2);
        for (std::size_t i = 0; i < (size_t)n; ++i) {
            if (i < 10) {
                mixed[i * 2] = nan;
                mixed[i * 2 + 1] = nan;
            } else {
                mixed[i * 2] = 0.25f;
                mixed[i * 2 + 1] = 0.25f;
            }
        }
        auto wm = lean::buildWaveform(mixed.data(), n, rate);
        wm.aggregate(0.0, wm.durationSeconds, mn, mx);
        check(std::isfinite(mx) && std::fabs(mx - 0.25f) < 1e-6f,
              "nonfinite: finite frames kept", (double)mx, 0.25);
        check(std::isfinite(mn) && std::fabs(mn) < 1e-12,
              "nonfinite: NaN frames dropped", (double)mn, 0.0);
    }

    // --- W8: zoom table clamps at both ends and "whole" maps to the full
    //     duration (or to zero for an invalid track length). ---
    {
        check(lean::clampZoomIndex(-5) == 0, "zoom: lower clamp",
              (double)lean::clampZoomIndex(-5), 0.0);
        check(lean::clampZoomIndex(8) == 7, "zoom: upper clamp",
              (double)lean::clampZoomIndex(8), 7.0);
        check(lean::clampZoomIndex(99) == 7, "zoom: far upper clamp",
              (double)lean::clampZoomIndex(99), 7.0);
        check(lean::clampZoomIndex(3) == 3, "zoom: identity",
              (double)lean::clampZoomIndex(3), 3.0);
        check(std::fabs(lean::zoomSpanSeconds(3, 300.0) - 32.0) < 1e-9,
              "zoom: 32 s entry", lean::zoomSpanSeconds(3, 300.0), 32.0);
        check(std::fabs(lean::zoomSpanSeconds(7, 600.0) - 600.0) < 1e-9,
              "zoom: whole maps to full length",
              lean::zoomSpanSeconds(7, 600.0), 600.0);
        check(std::fabs(lean::zoomSpanSeconds(0, 3.0) - 3.0) < 1e-9,
              "zoom: span >= length resolves to length",
              lean::zoomSpanSeconds(0, 3.0), 3.0);
        check(std::fabs(lean::zoomSpanSeconds(0, 10.0) - 4.0) < 1e-9,
              "zoom: 4 s entry", lean::zoomSpanSeconds(0, 10.0), 4.0);
        check(std::fabs(lean::zoomSpanSeconds(7, 0.0)) < 1e-9,
              "zoom: invalid length yields zero",
              lean::zoomSpanSeconds(7, 0.0), 0.0);
    }

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
