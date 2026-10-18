// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Newest-wins load gate: superseded generations must fail isCurrent/accept
// at every stage boundary, gates stay independent per deck, and concurrent
// submit/accept traffic never regresses or accepts a non-newest token.
#include "LoadSchedule.h"

#include <atomic>
#include <cstdio>
#include <thread>

namespace {

int failures = 0;

void check(bool cond, const char* name) {
    std::printf("LOADSCHEDULE_TEST %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        ++failures;
}

void testNewestWins() {
    lean::LoadGate gate;
    const uint64_t a = gate.submit();
    check(a == 1, "first token is 1");
    check(gate.isCurrent(a), "fresh token is current");
    check(gate.accept(a), "fresh result accepted");

    const uint64_t b = gate.submit();
    check(b == 2, "tokens increase");
    check(!gate.isCurrent(a), "superseded worker stage aborts");
    check(!gate.accept(a), "superseded result never publishes");
    check(gate.isCurrent(b) && gate.accept(b), "newest token publishes");

    // Stage-boundary pattern: a job may pass an early check and fail later.
    lean::LoadGate g2;
    const uint64_t t1 = g2.submit();
    bool stage1 = g2.isCurrent(t1);
    const uint64_t t2 = g2.submit(); // user re-picked mid-pipeline
    bool stage2 = g2.isCurrent(t1);
    check(stage1 && !stage2, "mid-pipeline supersede detected");
    check(!g2.accept(t1), "mid-pipeline stale result rejected");
    check(g2.accept(t2), "re-submitted job publishes");
}

void testDecksIndependent() {
    lean::LoadGate deckA, deckB;
    const uint64_t a1 = deckA.submit();
    const uint64_t b1 = deckB.submit();
    const uint64_t a2 = deckA.submit();
    check(deckB.isCurrent(b1), "deck B unaffected by deck A submit");
    check(deckB.accept(b1), "deck B publishes");
    check(!deckA.isCurrent(a1) && deckA.isCurrent(a2),
          "deck A newest-wins unaffected by deck B");
}

void testConcurrent() {
    lean::LoadGate gate;
    constexpr uint64_t kSubmits = 5000;
    std::atomic<bool> done{ false };
    std::thread submitter([&] {
        for (uint64_t i = 0; i < kSubmits; ++i)
            gate.submit();
        done.store(true, std::memory_order_release);
    });
    uint64_t lastAccepted = 0;
    bool regressed = false;
    bool acceptedBeyondFinal = false;
    for (;;) {
        const uint64_t t = gate.current();
        if (gate.accept(t)) {
            if (t < lastAccepted)
                regressed = true;
            lastAccepted = t;
        }
        if (done.load(std::memory_order_acquire)) {
            // Drain once more, then verify the final generation.
            const uint64_t fin = gate.current();
            if (gate.accept(fin) && fin < lastAccepted)
                regressed = true;
            if (fin > kSubmits)
                acceptedBeyondFinal = true;
            if (gate.current() != kSubmits)
                acceptedBeyondFinal = true;
            break;
        }
    }
    submitter.join();
    check(!regressed, "accepted tokens never regress");
    check(!acceptedBeyondFinal, "final generation equals submit count");
    check(gate.accept(kSubmits), "final generation acceptable");
    check(gate.current() == kSubmits, "current == number of submits");
}

} // namespace

int main() {
    testNewestWins();
    testDecksIndependent();
    testConcurrent();
    if (failures == 0)
        std::printf("LOADSCHEDULE_TEST PASS\n");
    else
        std::printf("LOADSCHEDULE_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
