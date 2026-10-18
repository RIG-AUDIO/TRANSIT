// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Sync math gates: follower tempo matches the leader's effective BPM,
// clamps to the pitch range, reports unknown when either side lacks BPM.
#include "Deck.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool cond, const char* name, double got, double want) {
    std::printf("SYNC_TEST %s %s (got %.4f want %.4f)\n", cond ? "ok  " : "FAIL",
                name, got, want);
    if (!cond)
        ++failures;
}

int run() {
    // 128 track follows a 132.0-effective leader.
    check(std::abs(lean::Deck::syncTempoFor(128.0, 132.0) - 132.0 / 128.0) < 1e-12, "ratio",
          lean::Deck::syncTempoFor(128.0, 132.0), 132.0 / 128.0);
    // Identity: same tempo stays 1.0.
    check(std::abs(lean::Deck::syncTempoFor(120.0, 120.0) - 1.0) < 1e-12, "identity",
          lean::Deck::syncTempoFor(120.0, 120.0), 1.0);
    // Clamps to the pitch range.
    check(std::abs(lean::Deck::syncTempoFor(200.0, 40.0) - 0.25) < 1e-12, "low clamp",
          lean::Deck::syncTempoFor(200.0, 40.0), 0.25);
    check(std::abs(lean::Deck::syncTempoFor(60.0, 300.0) - 4.0) < 1e-12, "high clamp",
          lean::Deck::syncTempoFor(60.0, 300.0), 4.0);
    // Unknown either side.
    check(lean::Deck::syncTempoFor(0.0, 120.0) < 0.0, "own unknown",
          lean::Deck::syncTempoFor(0.0, 120.0), -1.0);
    check(lean::Deck::syncTempoFor(120.0, 0.0) < 0.0, "leader unknown",
          lean::Deck::syncTempoFor(120.0, 0.0), -1.0);

    if (failures == 0)
        std::printf("SYNC_TEST PASS\n");
    else
        std::printf("SYNC_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
