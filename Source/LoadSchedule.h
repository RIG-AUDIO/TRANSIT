// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <cstdint>

namespace lean {

// UI-facing states of a deck's async file pipeline.
enum class LoadState : int {
    Empty = 0,
    Loading,
    Analyzing,
    Ready,
    Error,
};

// Newest-wins generation gate for the async load pipeline. JUCE-free.
//
// Roles: the message thread calls submit() when a job starts (its token is
// returned), the worker calls isCurrent(token) between pipeline stages to
// abandon superseded work, and the message thread calls accept(token) when
// a finished result arrives — only the result of the newest submission may
// publish. One gate per deck; results from different decks are independent.
// All operations are single atomic RMW/loads on one counter, so any
// interleaving converges on the newest generation.
class LoadGate {
public:
    // Message thread: begin a new job; returns its generation token.
    uint64_t submit() {
        return gen.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    // Worker: false once a newer submit happened — abandon this pipeline.
    bool isCurrent(uint64_t token) const {
        return token == gen.load(std::memory_order_acquire);
    }
    // Message thread: publish only the newest generation's result.
    bool accept(uint64_t token) const {
        return token == gen.load(std::memory_order_acquire);
    }
    uint64_t current() const { return gen.load(std::memory_order_acquire); }

private:
    std::atomic<uint64_t> gen{ 0 };
};

} // namespace lean
