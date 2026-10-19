// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "Deck.h"

#include <algorithm>
#include <cmath>

namespace lean {

Deck::Deck() {
    auto empty = std::make_shared<DeckBuffer>();
    active = empty;
}

void Deck::load(std::vector<float> interleavedStereo, double sampleRate) {
    auto next = std::make_shared<DeckBuffer>();
    next->stereo = std::move(interleavedStereo);
    next->frames = static_cast<int>(next->stereo.size() / 2);
    next->rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    fileRate.store(next->rate, std::memory_order_seq_cst);
    // Publish length metadata immediately: adoptStaged only runs on the
    // audio thread during render, which never happens while paused, so the
    // UI would otherwise show the previous track's length until first Play.
    infoFrames.store(next->frames, std::memory_order_seq_cst);
    infoRate.store(next->rate, std::memory_order_seq_cst);
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        staged = std::move(next);
    }
    stagedReady.store(true, std::memory_order_seq_cst);
    position.store(0.0, std::memory_order_seq_cst);
    cueFrame = 0.0;
    playing.store(false, std::memory_order_seq_cst);
    stretchDirty.store(true, std::memory_order_seq_cst);
}

void Deck::setOutputRate(double rate) {
    outputRate.store(rate > 0.0 ? rate : 44100.0, std::memory_order_seq_cst);
    stretchDirty.store(true, std::memory_order_seq_cst);
}

void Deck::setTempo(double t) {
    tempo.store(std::clamp(t, 0.25, 4.0), std::memory_order_seq_cst);
}

void Deck::setKeylock(bool enabled) {
    keylock.store(enabled, std::memory_order_seq_cst);
    stretchDirty.store(true, std::memory_order_seq_cst);
}

void Deck::setKeylockEco(bool ecoMode) {
    eco.store(ecoMode, std::memory_order_seq_cst);
    stretchDirty.store(true, std::memory_order_seq_cst);
}

void Deck::setPositionSeconds(double seconds) {
    const double rate = fileRate.load(std::memory_order_seq_cst);
    const int frames = infoFrames.load(std::memory_order_seq_cst);
    position.store(std::clamp(seconds * rate, 0.0, (double)std::max(0, frames - 1)),
                   std::memory_order_seq_cst);
}

void Deck::setCuePointSeconds(double seconds) {
    const double rate = fileRate.load(std::memory_order_seq_cst);
    const int frames = infoFrames.load(std::memory_order_seq_cst);
    cueFrame = std::clamp(seconds * rate, 0.0, (double)std::max(0, frames - 1));
}

double Deck::getCuePointSeconds() const {
    const int frames = infoFrames.load(std::memory_order_seq_cst);
    if (frames <= 0)
        return 0.0;
    return cueFrame / fileRate.load(std::memory_order_seq_cst);
}

double Deck::getPositionSeconds() const {
    return position.load(std::memory_order_seq_cst)
        / fileRate.load(std::memory_order_seq_cst);
}

double Deck::getLengthSeconds() const {
    return (double)infoFrames.load(std::memory_order_seq_cst)
        / infoRate.load(std::memory_order_seq_cst);
}

int Deck::getStretchLatency() const {
    return stretchLatency.load(std::memory_order_seq_cst);
}

float Deck::cubic(float y0, float y1, float y2, float y3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * y1) + (-y0 + y2) * t
        + (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * t2
        + (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * t3);
}

float Deck::frame(const DeckBuffer& buf, int index, int channel) {
    index = std::clamp(index, 0, std::max(0, buf.frames - 1));
    return buf.stereo[(size_t)index * 2 + (size_t)(channel & 1)];
}

void Deck::adoptStaged() {
    if (!stagedReady.load(std::memory_order_seq_cst))
        return;
    std::unique_lock<std::mutex> lock(stageMutex, std::try_to_lock);
    if (!lock)
        return; // message thread is staging; retry next block, never block
    if (staged) {
        active = std::move(staged);
        infoFrames.store(active->frames, std::memory_order_seq_cst);
        infoRate.store(active->rate, std::memory_order_seq_cst);
    }
    stagedReady.store(false, std::memory_order_seq_cst);
}

void Deck::render(float* left, float* right, int numSamples) {
    if (!playing.load(std::memory_order_seq_cst) || !left || !right || numSamples <= 0)
        return;

    adoptStaged();

    // Stretcher (re)configuration happens here on the audio thread so the
    // render path can never observe a half-built stretcher. This performs a
    // bounded allocation, but only on user-initiated reconfigure
    // (load / keylock toggle / device rate change) — never in steady state.
    if (stretchDirty.load(std::memory_order_seq_cst)) {
        stretchDirty.store(false, std::memory_order_seq_cst);
        if (keylock.load(std::memory_order_seq_cst)) {
            const float rate =
                (float)outputRate.load(std::memory_order_seq_cst);
            if (eco.load(std::memory_order_seq_cst))
                stretch.presetCheaper(2, rate);
            else
                stretch.presetDefault(2, rate);
            stretch.setTransposeSemitones(0.0f);
            stretch.reset();
            stretchLatency.store(stretch.inputLatency() + stretch.outputLatency(),
                                 std::memory_order_seq_cst);
            stretchReady.store(true, std::memory_order_seq_cst);
        } else {
            stretchReady.store(false, std::memory_order_seq_cst);
            stretchLatency.store(0, std::memory_order_seq_cst);
        }
        stretchCarry = 0.0;
        tailOut = 0;
    }

    const DeckBuffer& buf = *active;
    if (buf.frames == 0)
        return;

    const double fileRateLocal = fileRate.load(std::memory_order_seq_cst);
    const double outputRateLocal = outputRate.load(std::memory_order_seq_cst);
    const double tempoLocal = tempo.load(std::memory_order_seq_cst);

    if (keylock.load(std::memory_order_seq_cst)
        && stretchReady.load(std::memory_order_seq_cst)) {
        renderStretch(buf, left, right, numSamples);
        return;
    }

    const double step = (fileRateLocal / outputRateLocal) * tempoLocal;
    if (!(step > 0.0) || !std::isfinite(step))
        return;
    renderResample(buf, left, right, numSamples, step);
}

void Deck::renderResample(const DeckBuffer& buf, float* left, float* right,
                          int numSamples, double step) {
    const float coeff = smoothCoeff(outputRate.load(std::memory_order_seq_cst));
    double pos = position.load(std::memory_order_seq_cst);
    for (int i = 0; i < numSamples; ++i) {
        if (pos >= buf.frames || !std::isfinite(pos)) {
            position.store(pos, std::memory_order_seq_cst);
            playing.store(false, std::memory_order_seq_cst);
            break;
        }
        const int idx = (int)pos;
        const float frac = (float)(pos - idx);
        const float g = gain.next(coeff);
        left[i] += cubic(frame(buf, idx - 1, 0), frame(buf, idx, 0),
                         frame(buf, idx + 1, 0), frame(buf, idx + 2, 0), frac)
            * g;
        right[i] += cubic(frame(buf, idx - 1, 1), frame(buf, idx, 1),
                          frame(buf, idx + 1, 1), frame(buf, idx + 2, 1), frac)
            * g;
        pos += step;
    }
    position.store(pos, std::memory_order_seq_cst);
}

void Deck::renderStretch(const DeckBuffer& buf, float* left, float* right,
                         int numSamples) {
    const double fileRateLocal = fileRate.load(std::memory_order_seq_cst);
    const double outputRateLocal = outputRate.load(std::memory_order_seq_cst);
    const double tempoLocal = tempo.load(std::memory_order_seq_cst);
    const float coeff = smoothCoeff(outputRateLocal);
    const double inputStep = fileRateLocal / outputRateLocal;
    double pos = position.load(std::memory_order_seq_cst);
    int done = 0;
    while (done < numSamples) {
        const int m = std::min(512, numSamples - done);
        double want = stretchCarry + (double)m * tempoLocal;
        int inN = (int)want;
        stretchCarry = want - inN;
        if (inN < 1)
            inN = 1; // only reachable on tiny tail chunks; negligible drift
        if (inN > 2048) {
            stretchCarry += (double)(inN - 2048);
            inN = 2048;
        }

        float inL[2048], inR[2048];
        const bool eof = pos >= buf.frames;
        if (!eof) {
            for (int j = 0; j < inN; ++j) {
                const double p = pos + (double)j * inputStep;
                const int idx = (int)p;
                const float frac = (float)(p - idx);
                inL[j] = cubic(frame(buf, idx - 1, 0), frame(buf, idx, 0),
                               frame(buf, idx + 1, 0), frame(buf, idx + 2, 0), frac);
                inR[j] = cubic(frame(buf, idx - 1, 1), frame(buf, idx, 1),
                               frame(buf, idx + 1, 1), frame(buf, idx + 2, 1), frac);
            }
            pos += (double)inN * inputStep;
        } else {
            for (int j = 0; j < inN; ++j)
                inL[j] = inR[j] = 0.0f;
            tailOut += m;
        }
        position.store(pos, std::memory_order_seq_cst);

        float outL[512], outR[512];
        float* inB[2] = { inL, inR };
        float* outB[2] = { outL, outR };
        stretch.process(inB, inN, outB, m);
        for (int i = 0; i < m; ++i) {
            const float g = gain.next(coeff);
            left[done + i] += outL[i] * g;
            right[done + i] += outR[i] * g;
        }
        done += m;

        if (eof && tailOut > stretch.outputLatency() + 512) {
            playing.store(false, std::memory_order_seq_cst);
            break;
        }
    }
}

} // namespace lean
