// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Pioneer DDJ-FLX4 factory mapping: hardware protocol facts as pure data.
//
// Provenance (clean room): control -> byte bindings below are facts from
// AlphaTheta's official "DDJ-FLX4 MIDI message List" (Ver 1.0) corroborated
// by community hardware-monitor reverse engineering. No code or creative
// expression from any third-party mapping is used; behaviour (pickup,
// mailbox, folding) is original TRANSIT logic. Rows marked "verify" in
// docs/development/MIDI_MAPPINGS.md need a pass with a MIDI monitor on
// real hardware.
//
// JUCE-free and header-only so the table, the relative-encoder decode, the
// action mailbox and the soft-takeover rule are gated by plain unit tests
// (Tests/MidiMapTest.cpp). Channels here are 1-based (JUCE convention):
// hardware ch0 (deck 1) = 1, ch1 (deck 2) = 2, ch6 (mixer/browse) = 7.
#pragma once

#include "MidiMap.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lean {

// Button / relative-encoder events. Notes and jog/browse CCs land here and
// are applied on the message thread (transport toggles need the UI in the
// loop); continuous values keep the audio-thread MidiGrid path.
enum class Flx4Action : std::uint8_t {
    None = 0,
    Play,
    Cue,
    Sync,
    Pfl,
    Shift,
    Load,
    BrowsePress,
    BrowseScroll,
    JogNudge,
    JogSearch,
};

struct Flx4ActionMsg {
    std::uint8_t action = 0; // (uint8_t)Flx4Action
    std::uint8_t deck = 0;   // 0..1
    std::uint8_t pressed = 0; // buttons: 1 down / 0 up; encoders: unused
    std::int8_t delta = 0;    // encoders: relative steps (-64..+63)
};

// Single-producer (MIDI thread) / single-consumer (message thread) ring of
// action messages. Same discipline as MidiQueue: fixed capacity, newest
// drop on overflow, seq_cst throughout, no allocation.
class Flx4ActionQueue {
public:
    bool push(const Flx4ActionMsg& msg) {
        const std::uint64_t w = head.load(std::memory_order_seq_cst);
        const std::uint64_t t = tail.load(std::memory_order_seq_cst);
        if (w - t >= kCap) {
            dropped.fetch_add(1, std::memory_order_seq_cst);
            return false;
        }
        buf[w & kMask] = msg;
        head.store(w + 1, std::memory_order_seq_cst);
        return true;
    }

    int drain(Flx4ActionMsg* out, int maxOut) {
        int n = 0;
        std::uint64_t t = tail.load(std::memory_order_seq_cst);
        const std::uint64_t w = head.load(std::memory_order_seq_cst);
        while (t < w && n < maxOut) {
            out[n++] = buf[t & kMask];
            ++t;
        }
        tail.store(t, std::memory_order_seq_cst);
        return n;
    }

    long long droppedCount() const { return dropped.load(std::memory_order_seq_cst); }

private:
    static constexpr std::uint64_t kCap = 64;
    static constexpr std::uint64_t kMask = kCap - 1;
    Flx4ActionMsg buf[kCap] = {};
    std::atomic<std::uint64_t> head{ 0 };
    std::atomic<std::uint64_t> tail{ 0 };
    std::atomic<long long> dropped{ 0 };
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "atomic<uint64_t> must be lock-free");

// One value binding: 14-bit CC pair (LSB = msb + 0x20) -> target.
struct Flx4ValueBind {
    std::uint8_t ch = 0;  // 1-based
    std::uint8_t msb = 0; // 0..95 (LSB = msb + 0x20 must fit 0..127)
    std::int8_t deck = 0; // 0..1 (global targets use 0)
    MidiTarget target = MidiTarget::None;
};

// One action binding: note or relative CC -> Flx4Action.
struct Flx4ActionBind {
    std::uint8_t kind = 0; // 0 = CC, 1 = note
    std::uint8_t ch = 0;   // 1-based
    std::uint8_t num = 0;  // note or CC number
    Flx4Action action = Flx4Action::None;
    std::int8_t deck = 0;
};

// LED notes (MIDI out, same numbering as the input notes).
struct Flx4Leds {
    static constexpr std::uint8_t play = 0x0B;
    static constexpr std::uint8_t cue = 0x0C;
    static constexpr std::uint8_t sync = 0x58;
    static constexpr std::uint8_t pfl = 0x54;
    static constexpr std::uint8_t deck1Ch = 1;
    static constexpr std::uint8_t deck2Ch = 2;
};

// Position-resync SysEx (community RE, official list silent): makes a fresh
// connection report every analog control's position, doubling as keep-alive.
inline constexpr std::uint8_t kFlx4ResyncSysEx[] = {
    0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x05, 0x00, 0x50, 0x02, 0xF7
};

// Value table: deck sections on ch 1/2, mixer on ch 7. CFX knobs ride the
// per-deck AUX send (hardware filter position -> send level).
inline const Flx4ValueBind* flx4ValueTable(int& count) {
    static const Flx4ValueBind table[] = {
        // deck 1 (ch 1) / deck 2 (ch 2): trim, EQ, tempo, channel fader
        { 1, 0x00, 0, MidiTarget::Tempo }, { 2, 0x00, 1, MidiTarget::Tempo },
        { 1, 0x04, 0, MidiTarget::Gain },  { 2, 0x04, 1, MidiTarget::Gain },
        { 1, 0x07, 0, MidiTarget::High },  { 2, 0x07, 1, MidiTarget::High },
        { 1, 0x0B, 0, MidiTarget::Mid },   { 2, 0x0B, 1, MidiTarget::Mid },
        { 1, 0x0F, 0, MidiTarget::Low },   { 2, 0x0F, 1, MidiTarget::Low },
        { 1, 0x13, 0, MidiTarget::Fader }, { 2, 0x13, 1, MidiTarget::Fader },
        // mixer (ch 7): crossfader, phones mix, CFX -> per-deck send
        { 7, 0x1F, 0, MidiTarget::Xfade },
        { 7, 0x0C, 0, MidiTarget::Blend },
        { 7, 0x17, 0, MidiTarget::Send },
        { 7, 0x18, 1, MidiTarget::Send },
    };
    count = (int)(sizeof(table) / sizeof(table[0]));
    return table;
}

// Action table: transport/PFL/shift notes per deck, browse/load on ch 7,
// jog and browse encoders as relative CCs. SHIFT+jog (CC 0x29) searches.
inline const Flx4ActionBind* flx4ActionTable(int& count) {
    static const Flx4ActionBind table[] = {
        { 1, 1, 0x0B, Flx4Action::Play, 0 },
        { 1, 2, 0x0B, Flx4Action::Play, 1 },
        { 1, 1, 0x0C, Flx4Action::Cue, 0 },
        { 1, 2, 0x0C, Flx4Action::Cue, 1 },
        { 1, 1, 0x58, Flx4Action::Sync, 0 },
        { 1, 2, 0x58, Flx4Action::Sync, 1 },
        { 1, 1, 0x54, Flx4Action::Pfl, 0 },
        { 1, 2, 0x54, Flx4Action::Pfl, 1 },
        { 1, 1, 0x3F, Flx4Action::Shift, 0 },
        { 1, 2, 0x3F, Flx4Action::Shift, 1 },
        { 1, 7, 0x46, Flx4Action::Load, 0 },
        { 1, 7, 0x47, Flx4Action::Load, 1 },
        { 1, 7, 0x41, Flx4Action::BrowsePress, 0 }, // verify on hardware
        { 0, 7, 0x40, Flx4Action::BrowseScroll, 0 },
        // Jog: rubber side strip and platter nudge (vinyl-on platter also
        // nudges; scratch-style positioning is not implemented yet).
        { 0, 1, 0x21, Flx4Action::JogNudge, 0 },
        { 0, 2, 0x21, Flx4Action::JogNudge, 1 },
        { 0, 1, 0x22, Flx4Action::JogNudge, 0 },
        { 0, 2, 0x22, Flx4Action::JogNudge, 1 },
        { 0, 1, 0x23, Flx4Action::JogNudge, 0 },
        { 0, 2, 0x23, Flx4Action::JogNudge, 1 },
        { 0, 1, 0x29, Flx4Action::JogSearch, 0 },
        { 0, 2, 0x29, Flx4Action::JogSearch, 1 },
    };
    count = (int)(sizeof(table) / sizeof(table[0]));
    return table;
}

inline const Flx4ActionBind* flx4FindAction(int kind, int ch, int num) {
    int n = 0;
    const Flx4ActionBind* table = flx4ActionTable(n);
    for (int i = 0; i < n; ++i)
        if (table[i].kind == kind && table[i].ch == ch && table[i].num == num)
            return &table[i];
    return nullptr;
}

inline const Flx4ValueBind* flx4FindValue(int ch, int msb) {
    int n = 0;
    const Flx4ValueBind* table = flx4ValueTable(n);
    for (int i = 0; i < n; ++i)
        if (table[i].ch == ch && table[i].msb == msb)
            return &table[i];
    return nullptr;
}

// Case-insensitive substring match on the JUCE device name ("DDJ-FLX4 ...").
inline bool flx4NameMatches(const char* name) {
    if (name == nullptr)
        return false;
    auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    };
    const char* needle = "ddj-flx4";
    for (const char* p = name; *p != '\0'; ++p) {
        const char* a = p;
        const char* b = needle;
        while (*a != '\0' && *b != '\0' && lower(*a) == *b) {
            ++a;
            ++b;
        }
        if (*b == '\0')
            return true;
    }
    return false;
}

// Relative encoder decode: 0x40 = center, +1 per CW detent, two's
// complement for CCW (0x3F = -1). Returns -64..+63.
inline int flx4Relative(std::uint8_t v) { return (int)v - 0x40; }

// Soft takeover ("pickup") for absolute controls: the FLX4 is silent until
// a control is moved, so the first gesture must not jump the app value.
// A cell engages when the hardware value reaches or crosses the app value,
// then follows every later tick. Pure per-cell state; owned by the audio
// thread (single consumer), so no atomics inside.
class Pickup {
public:
    // cell: any index 0..kCells-1 (callers use deck*16 + target).
    // app/hw: 0..1. Returns the value to apply, or < 0 to ignore the tick.
    float process(int cell, float app, float hw) {
        if (cell < 0 || cell >= kCells)
            return hw;
        State& s = state[cell];
        if (s.engaged)
            return hw;
        const float eps = 2.0f / 127.0f;
        if (s.hasPrev && (s.prev - app) * (hw - app) <= 0.0f)
            s.engaged = true; // crossed (or reached) the app value
        else if ((hw - app <= eps && hw - app >= -eps))
            s.engaged = true; // landed on the app value
        s.prev = hw;
        s.hasPrev = true;
        return s.engaged ? hw : -1.0f;
    }

    void reset(int cell) {
        if (cell >= 0 && cell < kCells)
            state[cell] = State{};
    }

    static constexpr int kCells = 32; // 2 decks x 16 target slots

private:
    struct State {
        bool engaged = false;
        bool hasPrev = false;
        float prev = 0.0f;
    };
    State state[kCells];
};

} // namespace lean
