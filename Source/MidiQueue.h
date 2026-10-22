// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <cstdint>

namespace lean {

// Timestamped MIDI event, stamped with the engine's audio frame counter.
struct MidiEvent {
    uint64_t frame = 0;
    int channel = 1;
    int cc = 0;
    float value01 = 0.0f;
};

// Single-producer (MIDI thread) / single-consumer (audio thread) ring.
// Preallocated, lock-free. head is written only by push(), tail only by
// drain(); overflow drops the incoming (newest) event and counts it, so the
// producer never touches a slot the consumer may be reading.
class MidiQueue {
public:
    bool push(uint64_t frame, int channel, int cc, float value01) {
        const uint64_t w = head.load(std::memory_order_seq_cst);
        const uint64_t t = tail.load(std::memory_order_seq_cst);
        if (w - t >= kCap) {
            dropped.fetch_add(1, std::memory_order_seq_cst);
            return false;
        }
        buf[w & kMask] = MidiEvent{ frame, channel, cc, value01 };
        head.store(w + 1, std::memory_order_seq_cst);
        return true;
    }

    // Copy out up to maxOut events with frame <= uptoFrame. Returns count.
    int drain(uint64_t uptoFrame, MidiEvent* out, int maxOut) {
        int n = 0;
        uint64_t t = tail.load(std::memory_order_seq_cst);
        const uint64_t w = head.load(std::memory_order_seq_cst);
        while (t < w && n < maxOut && buf[t & kMask].frame <= uptoFrame) {
            out[n++] = buf[t & kMask];
            ++t;
        }
        tail.store(t, std::memory_order_seq_cst);
        return n;
    }

    long long droppedCount() const { return dropped.load(std::memory_order_seq_cst); }

private:
    static constexpr uint64_t kCap = 256;
    static constexpr uint64_t kMask = kCap - 1;
    MidiEvent buf[kCap] = {};
    std::atomic<uint64_t> head{ 0 };
    std::atomic<uint64_t> tail{ 0 };
    std::atomic<long long> dropped{ 0 };
};

} // namespace lean
