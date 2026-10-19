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
    Phones // appended last: persisted maps keep working
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
        case MidiTarget::Blend: return v; // 0..1
    }
    return v;
}

// Realtime MIDI routing grid: channel x CC -> packed binding code.
// Message thread binds, audio thread looks up; every cell is atomic, no
// allocation, no locks. Packed code: target+1 in the low 8 bits, deck in
// bit 8; -1 means empty.
class MidiGrid {
public:
    MidiGrid() { clear(); }

    void bind(int channel, int cc, int deck, MidiTarget target) {
        if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
            return;
        const int code = ((deck & 1) << 8) | ((int)target + 1);
        cells[channel - 1][cc].store(code, std::memory_order_seq_cst);
    }

    void clear(int channel, int cc) {
        if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
            return;
        cells[channel - 1][cc].store(-1, std::memory_order_seq_cst);
    }

    void clear() {
        for (auto& ch : cells)
            for (auto& cell : ch)
                cell.store(-1, std::memory_order_seq_cst);
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
    std::atomic<int> cells[16][128];
};

static_assert(std::atomic<int>::is_always_lock_free,
              "atomic<int> must be lock-free");

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
                || target > (int)MidiTarget::Phones)
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
