// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// MIDI queue timing gates: frame-accurate drain deadlines, FIFO ordering
// under a live producer, and drop-newest overflow accounting. JUCE-free.
#include "MidiQueue.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* name) {
    std::printf("MIDIQUEUE_TEST %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        ++failures;
}

void testDeadlineAndOrder() {
    lean::MidiQueue q;
    check(q.push(10, 1, 7, 0.5f), "push frame 10");
    check(q.push(20, 1, 8, 0.25f), "push frame 20");
    check(q.push(20, 1, 9, 0.75f), "push frame 20 (second)");

    lean::MidiEvent ev[8];
    // Deadline below every stamp: nothing drains yet.
    check(q.drain(0, ev, 8) == 0, "future frames deferred");
    check(q.drain(9, ev, 8) == 0, "deadline below first frame defers");
    // Deadline reaches frame 10 only: exactly the first event, in order.
    check(q.drain(10, ev, 8) == 1, "frame-10 deadline drains one");
    check(ev[0].cc == 7 && ev[0].frame == 10, "drained event matches stamp");
    // Frame 20 pair drains together and preserves push order.
    check(q.drain(100, ev, 8) == 2, "later deadline drains the rest");
    check(ev[0].cc == 8 && ev[1].cc == 9, "FIFO order across equal frames");
    check(q.drain(100, ev, 8) == 0, "queue empty after full drain");
}

void testCapacityAndOverflow() {
    lean::MidiQueue q;
    lean::MidiEvent ev[64];
    // Fill beyond capacity: exactly kCap pushes may succeed (drop-newest).
    int accepted = 0;
    for (uint64_t i = 1; i <= 300; ++i)
        if (q.push(i, 1, 0, 0.0f))
            ++accepted;
    check(accepted == 256, "capacity honored under overflow");
    check(q.droppedCount() == 44, "dropped counted for rejected newest");
    // Max-out cap: drains return at most maxOut and keep the remainder.
    int total = 0;
    int got = 0;
    while ((got = q.drain(1000, ev, 64)) > 0)
        total += got;
    check(total == 256, "all accepted events drain across calls");
}

void testConcurrentProducer() {
    lean::MidiQueue q;
    constexpr int kPush = 4000;
    std::atomic<bool> done{ false };
    std::thread producer([&] {
        for (uint64_t i = 1; i <= (uint64_t)kPush; ++i)
            q.push(i, 1, (int)(i % 128), 0.5f);
        done.store(true, std::memory_order_seq_cst);
    });
    long long drained = 0;
    lean::MidiEvent ev[64];
    uint64_t lastFrame = 0;
    bool monotonic = true;
    for (;;) {
        const int n = q.drain((uint64_t)kPush, ev, 64);
        for (int i = 0; i < n; ++i) {
            if (ev[i].frame < lastFrame)
                monotonic = false;
            lastFrame = ev[i].frame;
        }
        drained += n;
        if (done.load(std::memory_order_seq_cst) && n == 0) {
            const int tail = q.drain((uint64_t)kPush, ev, 64);
            drained += tail;
            if (tail == 0)
                break;
        }
    }
    producer.join();
    check(monotonic, "frames never regress under concurrency");
    check(drained + q.droppedCount() == kPush,
          "drained + dropped == pushed (no silent loss)");
    check(q.droppedCount() >= 0, "drop counter non-negative");
}

} // namespace

int main() {
    testDeadlineAndOrder();
    testCapacityAndOverflow();
    testConcurrentProducer();
    if (failures == 0)
        std::printf("MIDIQUEUE_TEST PASS\n");
    else
        std::printf("MIDIQUEUE_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
