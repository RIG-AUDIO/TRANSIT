// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "Mixer.h"

#include <algorithm>
#include <cmath>

namespace lean {
namespace {

constexpr double kPi = 3.14159265358979323846;

double clampDb(double db) {
    return std::clamp(db, -24.0, 6.0);
}

} // namespace

void Biquad::setLowShelf(double fs, double freq, double gainDb) {
    const double A = pow(10.0, clampDb(gainDb) / 40.0);
    const double w0 = 2.0 * kPi * freq / fs;
    const double cosw = cos(w0), sinw = sin(w0);
    const double alpha = sinw / 2.0 * sqrt(2.0);
    const double sqrtA = sqrt(A);
    double b0v = A * ((A + 1.0) - (A - 1.0) * cosw + 2.0 * sqrtA * alpha);
    double b1v = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
    double b2v = A * ((A + 1.0) - (A - 1.0) * cosw - 2.0 * sqrtA * alpha);
    double a0v = (A + 1.0) + (A - 1.0) * cosw + 2.0 * sqrtA * alpha;
    double a1v = -2.0 * ((A - 1.0) + (A + 1.0) * cosw);
    double a2v = (A + 1.0) + (A - 1.0) * cosw - 2.0 * sqrtA * alpha;
    b0 = (float)(b0v / a0v);
    b1 = (float)(b1v / a0v);
    b2 = (float)(b2v / a0v);
    a1 = (float)(a1v / a0v);
    a2 = (float)(a2v / a0v);
}

void Biquad::setPeaking(double fs, double freq, double q, double gainDb) {
    const double A = pow(10.0, clampDb(gainDb) / 40.0);
    const double w0 = 2.0 * kPi * freq / fs;
    const double cosw = cos(w0), sinw = sin(w0);
    const double alpha = sinw / (2.0 * q);
    double b0v = 1.0 + alpha * A;
    double b1v = -2.0 * cosw;
    double b2v = 1.0 - alpha * A;
    double a0v = 1.0 + alpha / A;
    double a1v = -2.0 * cosw;
    double a2v = 1.0 - alpha / A;
    b0 = (float)(b0v / a0v);
    b1 = (float)(b1v / a0v);
    b2 = (float)(b2v / a0v);
    a1 = (float)(a1v / a0v);
    a2 = (float)(a2v / a0v);
}

void Biquad::setHighShelf(double fs, double freq, double gainDb) {
    const double A = pow(10.0, clampDb(gainDb) / 40.0);
    const double w0 = 2.0 * kPi * freq / fs;
    const double cosw = cos(w0), sinw = sin(w0);
    const double alpha = sinw / 2.0 * sqrt(2.0);
    const double sqrtA = sqrt(A);
    double b0v = A * ((A + 1.0) + (A - 1.0) * cosw + 2.0 * sqrtA * alpha);
    double b1v = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
    double b2v = A * ((A + 1.0) + (A - 1.0) * cosw - 2.0 * sqrtA * alpha);
    double a0v = (A + 1.0) - (A - 1.0) * cosw + 2.0 * sqrtA * alpha;
    double a1v = 2.0 * ((A - 1.0) - (A + 1.0) * cosw);
    double a2v = (A + 1.0) - (A - 1.0) * cosw - 2.0 * sqrtA * alpha;
    b0 = (float)(b0v / a0v);
    b1 = (float)(b1v / a0v);
    b2 = (float)(b2v / a0v);
    a1 = (float)(a1v / a0v);
    a2 = (float)(a2v / a0v);
}

void Eq3::set(double sampleRate, double lowDb, double midDb, double highDb) {
    this->fs.store(sampleRate > 0.0 ? sampleRate : 44100.0,
                   std::memory_order_relaxed);
    gains[0].store(clampDb(lowDb), std::memory_order_relaxed);
    gains[1].store(clampDb(midDb), std::memory_order_relaxed);
    gains[2].store(clampDb(highDb), std::memory_order_relaxed);
    dirty.store(true, std::memory_order_release);
}

void Eq3::setRate(double sampleRate) {
    this->fs.store(sampleRate > 0.0 ? sampleRate : 44100.0,
                   std::memory_order_relaxed);
    dirty.store(true, std::memory_order_release);
}

void Eq3::updateIfNeeded() {
    if (!dirty.load(std::memory_order_acquire))
        return;
    dirty.store(false, std::memory_order_relaxed);
    const double rate = fs.load(std::memory_order_relaxed);
    const double lo = gains[0].load(std::memory_order_relaxed);
    const double mi = gains[1].load(std::memory_order_relaxed);
    const double hi = gains[2].load(std::memory_order_relaxed);
    for (int ch = 0; ch < 2; ++ch) {
        bands[0][ch].setLowShelf(rate, 250.0, lo);
        bands[1][ch].setPeaking(rate, 1000.0, 0.9, mi);
        bands[2][ch].setHighShelf(rate, 4000.0, hi);
    }
}

void Eq3::reset() {
    for (auto& b : bands)
        for (auto& q : b)
            q.reset();
}

float Eq3::process(int channel, float x) {
    float y = bands[0][channel & 1].process(x);
    y = bands[1][channel & 1].process(y);
    return bands[2][channel & 1].process(y);
}

void Mixer::setOutputRate(double rate) {
    outputRate.store(rate > 0.0 ? rate : 44100.0, std::memory_order_relaxed);
    for (auto& s : strips) {
        s.deck.setOutputRate(rate);
        s.eq.setRate(rate);
    }
}

void Mixer::setEq(int ch, double lowDb, double midDb, double highDb) {
    strips[ch & 1].eq.set(outputRate.load(std::memory_order_relaxed), lowDb,
                          midDb, highDb);
}

void Mixer::reset() {
    for (auto& s : strips)
        s.eq.reset();
}

void Mixer::eqGains(int ch, double& lo, double& mi, double& hi) const {
    const ChannelStrip& s = strips[ch & 1];
    lo = s.eq.gainDb(0);
    mi = s.eq.gainDb(1);
    hi = s.eq.gainDb(2);
}

void Mixer::snap() {
    for (auto& s : strips) {
        s.trim.snap(s.trim.next(1.0f));
        s.fader.snap(s.fader.next(1.0f));
        for (auto& send : s.sends)
            send.snap(send.next(1.0f));
    }
    xfade.snap(xfade.next(1.0f));
    master.snap(master.next(1.0f));
    cueBlend.snap(cueBlend.next(1.0f));
    phonesLevel.snap(phonesLevel.next(1.0f));
    for (auto& ret : auxReturn)
        ret.snap(ret.next(1.0f));
}

void Mixer::render(float* left, float* right, float* phonesL, float* phonesR,
                   int numSamples) {
    if (!left || !right || numSamples <= 0)
        return;
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);
    stripPeak[0] = stripPeak[1] = 0.0f; // per-block peaks for channel meters
    if (auxHost)
        auxHost->blockBoundary(); // once per block, even when all sends are silent
    const bool doPhones = phonesL && phonesR;
    if (doPhones) {
        std::fill(phonesL, phonesL + numSamples, 0.0f);
        std::fill(phonesR, phonesR + numSamples, 0.0f);
    }

    const double rate = outputRate.load(std::memory_order_relaxed);
    const float coeff = smoothCoeff(rate);

    // Outer chunk loop keeps all scratch storage on the stack: no allocation
    // on the audio thread regardless of the device block size.
    int done = 0;
    while (done < numSamples) {
        const int m = std::min(512, numSamples - done);
        float cueL[512], cueR[512];
        float auxL[kAuxBuses][512], auxR[kAuxBuses][512];
        for (int i = 0; i < m; ++i) {
            cueL[i] = cueR[i] = 0.0f;
            for (int a = 0; a < kAuxBuses; ++a)
                auxL[a][i] = auxR[a][i] = 0.0f;
        }

        for (int s = 0; s < kChannels; ++s) {
            ChannelStrip& strip = strips[s];
            strip.eq.updateIfNeeded();
            const bool playing = strip.deck.isPlaying();
            const bool wantCue = doPhones
                && strip.cue.load(std::memory_order_relaxed) && playing;
            if (!playing && !wantCue)
                continue;
            float tmpL[512], tmpR[512];
            for (int i = 0; i < m; ++i)
                tmpL[i] = tmpR[i] = 0.0f;
            strip.deck.render(tmpL, tmpR, m);
            for (int i = 0; i < m; ++i) {
                // Advance the shared crossfader once per sample (first strip);
                // the second strip reuses the same value.
                const float xf = (s == 0) ? xfade.next(coeff) : xfade.current();
                const float w = s == 0 ? (float)cos(xf * 0.5 * kPi)
                                       : (float)sin(xf * 0.5 * kPi);
                const float trimFader = strip.trim.next(coeff) * strip.fader.next(coeff);
                const float gMaster = trimFader * w;
                const float gCue = strip.trim.current();
                const float l = strip.eq.process(0, tmpL[i]);
                const float r = strip.eq.process(1, tmpR[i]);
                left[done + i] += l * gMaster;
                right[done + i] += r * gMaster;
                // Post-fader aux taps (pre-crossfader).
                for (int a = 0; a < kAuxBuses; ++a) {
                    const float gs = strip.sends[a].next(coeff);
                    auxL[a][i] += l * trimFader * gs;
                    auxR[a][i] += r * trimFader * gs;
                }
                // Channel meter: post-fader, pre-crossfader.
                const float pk = std::max(std::abs(l * trimFader),
                                          std::abs(r * trimFader));
                if (pk > stripPeak[s])
                    stripPeak[s] = pk;
                if (wantCue) {
                    cueL[i] += l * gCue;
                    cueR[i] += r * gCue;
                }
            }
        }
        // Aux inserts: skip silent buses so idle plugins cost nothing.
        if (auxHost) {
            for (int a = 0; a < kAuxBuses; ++a) {
                float peak = 0.0f;
                for (int i = 0; i < m; ++i)
                    peak = std::max(peak, std::max(std::abs(auxL[a][i]),
                                                   std::abs(auxR[a][i])));
                if (peak <= 0.0f)
                    continue;
                auxHost->processAux(a, auxL[a], auxR[a], m);
                const float ret = auxReturn[a].next(coeff);
                if (ret <= 0.0f) // gains never go negative: same as == 0
                    continue;
                for (int i = 0; i < m; ++i) {
                    left[done + i] += auxL[a][i] * ret;
                    right[done + i] += auxR[a][i] * ret;
                }
            }
        }
        if (doPhones) {
            for (int i = 0; i < m; ++i) {
                const float blend = cueBlend.next(coeff);
                const float plevel = phonesLevel.next(coeff);
                const float mgain = master.next(coeff);
                left[done + i] *= mgain;
                right[done + i] *= mgain;
                phonesL[done + i] =
                    (blend * cueL[i] + (1.0f - blend) * left[done + i]) * plevel;
                phonesR[done + i] =
                    (blend * cueR[i] + (1.0f - blend) * right[done + i]) * plevel;
            }
        } else {
            for (int i = 0; i < m; ++i) {
                const float mgain = master.next(coeff);
                left[done + i] *= mgain;
                right[done + i] *= mgain;
            }
        }
        done += m;
    }
}

} // namespace lean
