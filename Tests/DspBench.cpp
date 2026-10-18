// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// One-off DSP load probe (not a gate): renders 30 s of stereo through the
// real mixer paths and reports the realtime multiple. Run manually to decide
// whether hand-rolled SIMD is worth it.
#include "Mixer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kRate = 48000.0;

std::vector<float> musicLike(int frames) {
    std::vector<float> buf((size_t)frames * 2);
    for (int i = 0; i < frames; ++i) {
        const double t = (double)i / kRate;
        const float s = 0.3f * (float)sin(2.0 * 3.141592653589793 * 110.0 * t)
            + 0.2f * (float)sin(2.0 * 3.141592653589793 * 440.0 * t)
            + 0.1f * (float)sin(2.0 * 3.141592653589793 * 3520.0 * t);
        buf[(size_t)i * 2] = s;
        buf[(size_t)i * 2 + 1] = s;
    }
    return buf;
}

double renderAll(lean::Mixer& m, int frames) {
    std::vector<float> L((size_t)frames, 0.0f), R((size_t)frames, 0.0f);
    const auto t0 = std::chrono::steady_clock::now();
    int done = 0;
    while (done < frames) {
        const int n = std::min(1024, frames - done);
        std::vector<float> bl((size_t)n, 0.0f), br((size_t)n, 0.0f);
        m.render(bl.data(), br.data(), n);
        done += n;
        if (!m.channel(0).deck.isPlaying() && !m.channel(1).deck.isPlaying())
            break;
    }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

int run() {
    const int frames = (int)kRate * 30;
    auto src = musicLike(frames);

    {
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.snap();
        for (int d = 0; d < 2; ++d) {
            m.channel(d).deck.load(src, kRate);
            m.channel(d).deck.setTempo(1.05);
            m.channel(d).deck.play(true);
        }
        m.setEq(0, -6.0, 3.0, 2.0);
        m.snap();
        const double s = renderAll(m, frames);
        std::printf("BENCH resample+EQ 2 decks: %.2f s audio in %.2f s wall (%.1fx)\n",
                    30.0, s, 30.0 / s);
    }
    {
        lean::Mixer m;
        m.setOutputRate(kRate);
        m.snap();
        for (int d = 0; d < 2; ++d) {
            m.channel(d).deck.load(src, kRate);
            m.channel(d).deck.setKeylock(true);
            m.channel(d).deck.setTempo(1.05);
            m.channel(d).deck.play(true);
        }
        m.setEq(0, -6.0, 3.0, 2.0);
        m.snap();
        const double s = renderAll(m, frames);
        std::printf("BENCH keylock+EQ 2 decks: %.2f s audio in %.2f s wall (%.1fx)\n",
                    30.0, s, 30.0 / s);
    }
    return 0;
}

} // namespace

int main() {
    return run();
}
