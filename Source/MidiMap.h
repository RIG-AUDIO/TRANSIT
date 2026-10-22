// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace lean {

// MIDI CC dispatcher. Pure C++, no dependencies. Channels 1-16, CC 0-127.
// Persistent map (MidiMap) lives in the UI; the realtime grid (MidiGrid)
// lives in the engine and is audio-safe.
enum class MidiTarget : int {
    None = -1,
    Tempo = 0,
    Gain,
    Fader,
    Low,
    Mid,
    High,
    Xfade,
    Master,
    Blend,
    Phones,
    Send, // appended last: persisted maps keep working
};

struct MidiBinding {
    int channel = 1; // 1..16
    int cc = 0;      // 0..127
    int deck = 0;    // 0..1, ignored for global targets
    MidiTarget target = MidiTarget::None;
};

inline bool isDeckTarget(MidiTarget t) {
    return t == MidiTarget::Tempo || t == MidiTarget::Gain || t == MidiTarget::Fader
        || t == MidiTarget::Low || t == MidiTarget::Mid || t == MidiTarget::High;
}

// CC 0..1 to control value in the target's native range. Pure, unit-tested.
inline float midiValue(MidiTarget t, float v01) {
    const float v = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
    switch (t) {
        case MidiTarget::Tempo: return 0.5f + v * 1.0f;
        case MidiTarget::Gain: return v * 1.25f;
        case MidiTarget::Master: return v * 1.25f;
        case MidiTarget::Phones: return v * 1.25f;
        case MidiTarget::Low:
        case MidiTarget::Mid:
        case MidiTarget::High: return -24.0f + v * 30.0f;
        case MidiTarget::None:
        case MidiTarget::Fader:
        case MidiTarget::Xfade:
        case MidiTarget::Blend:
        case MidiTarget::Send: return v; // 0..1
    }
    return v;
}

// Realtime MIDI routing grid: channel x CC -> packed binding code.
// Message thread binds, audio thread looks up; every cell is atomic, no
// allocation, no locks. Packed code: target+1 in the low 8 bits, deck in
// bit 8, 14-bit marker in bit 17; -1 means empty.
//
    // 14-bit controls (standard pairing: MSB on `cc`, LSB on `cc + 0x20`) are
    // folded on the MIDI thread in ingest14(): each half updates its atomics
    // and emits the combined 0..16383 value immediately, so the value queue
    // stays 7-bit-agnostic and never waits for the partner half.
class MidiGrid {
public:
    MidiGrid() { clear(); }

    void bind(int channel, int cc, int deck, MidiTarget target) {
        if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
            return;
        dropPairing(channel, cc);
        const int code = ((deck & 1) << 8) | ((int)target + 1);
        cells[channel - 1][cc].store(code, std::memory_order_seq_cst);
    }

    // 14-bit binding: MSB on `msb`, LSB on the standard MSB+0x20 partner.
    void bind14(int channel, int msb, int deck, MidiTarget target) {
        if (channel < 1 || channel > 16 || msb < 0 || msb > 95)
            return;
        dropPairing(channel, msb);
        dropPairing(channel, msb + 0x20);
        const int code = k14Bit | ((deck & 1) << 8) | ((int)target + 1);
        cells[channel - 1][msb].store(code, std::memory_order_seq_cst);
        lsbOwner[channel - 1][msb + 0x20].store(msb, std::memory_order_seq_cst);
    }

    // MIDI-thread fold for one half of a 14-bit pair. Returns the combined
    // 0..16383 value, or -1 when `cc` is not part of a bound 14-bit cell
    // (the caller falls back to the plain 7-bit path). `msbCc` (optional)
    // receives the cell's MSB CC, so the caller can queue the event under
    // the bound (MSB) number even when only the LSB half moved — otherwise
    // the audio thread's cell lookup would miss the fine LSB-only steps.
    int ingest14(int channel, int cc, int v7, int* msbCc = nullptr) {
        if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
            return -1;
        const int code = cells[channel - 1][cc].load(std::memory_order_seq_cst);
        if (code >= 0 && (code & k14Bit) != 0) {
            if (msbCc != nullptr)
                *msbCc = cc;
            msb7[channel - 1][cc].store((unsigned char)v7,
                                        std::memory_order_seq_cst);
            const int lo = lsb7[channel - 1][cc + 0x20].load(
                std::memory_order_seq_cst);
            return (v7 << 7) | (int)lo;
        }
        const int owner = lsbOwner[channel - 1][cc].load(std::memory_order_seq_cst);
        if (owner >= 0) {
            if (msbCc != nullptr)
                *msbCc = owner;
            lsb7[channel - 1][cc].store((unsigned char)v7,
                                        std::memory_order_seq_cst);
            const int hi = msb7[channel - 1][owner].load(std::memory_order_seq_cst);
            return ((int)hi << 7) | v7;
        }
        return -1;
    }

    void clear(int channel, int cc) {
        if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
            return;
        dropPairing(channel, cc);
        cells[channel - 1][cc].store(-1, std::memory_order_seq_cst);
    }

    void clear() {
        for (int ch = 0; ch < 16; ++ch) {
            for (int cc = 0; cc < 128; ++cc) {
                cells[ch][cc].store(-1, std::memory_order_seq_cst);
                lsbOwner[ch][cc].store(-1, std::memory_order_seq_cst);
                msb7[ch][cc].store(0, std::memory_order_seq_cst);
                lsb7[ch][cc].store(0, std::memory_order_seq_cst);
            }
        }
    }

    // Returns packed code or -1. Decode with gridDeck()/gridTarget().
    int lookup(int channel, int cc) const {
        if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
            return -1;
        return cells[channel - 1][cc].load(std::memory_order_seq_cst);
    }

    static int gridDeck(int code) { return (code >> 8) & 1; }
    static MidiTarget gridTarget(int code) { return (MidiTarget)((code & 0xff) - 1); }

private:
    // Drops the MSB/LSB association when either half is (re)bound or
    // cleared, so a stale owner can never fold into an unrelated cell.
    void dropPairing(int channel, int cc) {
        lsbOwner[channel - 1][cc].store(-1, std::memory_order_seq_cst);
        const int old = cells[channel - 1][cc].load(std::memory_order_seq_cst);
        if (old >= 0 && (old & k14Bit) != 0 && cc + 0x20 <= 127)
            lsbOwner[channel - 1][cc + 0x20].store(-1, std::memory_order_seq_cst);
    }

    static constexpr int k14Bit = 1 << 17;

    std::atomic<int> cells[16][128];
    std::atomic<int> lsbOwner[16][128]; // LSB cc -> MSB cc, -1 empty
    std::atomic<unsigned char> msb7[16][128];
    std::atomic<unsigned char> lsb7[16][128];
};

static_assert(std::atomic<int>::is_always_lock_free,
              "atomic<int> must be lock-free");
static_assert(std::atomic<unsigned char>::is_always_lock_free,
              "atomic<unsigned char> must be lock-free");

class MidiMap {
public:
    void bind(int channel, int cc, int deck, MidiTarget target) {
        clear(channel, cc);
        bindings.push_back({ channel, cc, deck, target });
    }

    void clear(int channel, int cc) {
        for (auto it = bindings.begin(); it != bindings.end();) {
            if (it->channel == channel && it->cc == cc)
                it = bindings.erase(it);
            else
                ++it;
        }
    }

    std::optional<MidiBinding> lookup(int channel, int cc) const {
        for (const auto& b : bindings)
            if (b.channel == channel && b.cc == cc)
                return b;
        return std::nullopt;
    }

    size_t size() const { return bindings.size(); }
    const std::vector<MidiBinding>& all() const { return bindings; }

    std::string save() const {
        std::ostringstream out;
        for (const auto& b : bindings)
            out << b.channel << ' ' << b.cc << ' ' << b.deck << ' '
                << (int)b.target << '\n';
        return out.str();
    }

    bool load(const std::string& text) {
        std::vector<MidiBinding> next;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream row(line);
            MidiBinding b;
            int target = -1;
            if (!(row >> b.channel >> b.cc >> b.deck >> target))
                return false;
            if (b.channel < 1 || b.channel > 16 || b.cc < 0 || b.cc > 127
                || b.deck < 0 || b.deck > 1 || target < 0
                || target > (int)MidiTarget::Send)
                return false;
            b.target = (MidiTarget)target;
            next.push_back(b);
        }
        bindings = std::move(next);
        return true;
    }

private:
    std::vector<MidiBinding> bindings;
};

} // namespace lean
