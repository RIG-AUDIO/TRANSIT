// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// C4 / S1 / S2 protocol gates for FxRack (no VST3 files on the runner —
// an in-process stub AudioPluginInstance stands in):
//  - suspend handshake against a simulated audio block boundary;
//  - staged pickup deferred while a suspend is held, applied after;
//  - retire deferral when the retire lock is busy;
//  - retire capacity deferral (no push beyond reserved capacity);
//  - every stub destroyed exactly once (dtor/LSan hygiene).
#include "FxRack.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

namespace {

int failures = 0;

void check(bool cond, const char* name) {
    std::printf("FXRACK_TEST %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        ++failures;
}

std::atomic<int> gCreated{ 0 };
std::atomic<int> gDestroyed{ 0 };

class StubPlugin : public juce::AudioPluginInstance {
public:
    StubPlugin() { gCreated.fetch_add(1, std::memory_order_relaxed); }
    ~StubPlugin() override { gDestroyed.fetch_add(1, std::memory_order_relaxed); }

    void fillInPluginDescription(juce::PluginDescription&) const override {}
    const juce::String getName() const override { return "Stub"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer&) override {
        // Observable action: halve the aux send. Proves the ACTIVE slot ran.
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            float* d = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                d[i] *= 0.5f;
        }
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

} // namespace

// Test seam (review C4): named exactly as declared friend in FxRack.h —
// global scope, not the anonymous namespace above.
struct FxRackTestAccess {
    static void stage(FxRack& r, int slot,
                      std::unique_ptr<juce::AudioPluginInstance> p) {
        std::lock_guard<std::mutex> lock(r.stageMutex);
        r.slots[(size_t)slot].staged = std::move(p);
        r.slots[(size_t)slot].stagedReady.store(true,
                                                 std::memory_order_release);
        r.slots[(size_t)slot].name = "Stub";
    }
    static juce::AudioPluginInstance* active(FxRack& r, int slot) {
        return r.slots[(size_t)slot].active.get();
    }
    static bool stagedReady(FxRack& r, int slot) {
        return r.slots[(size_t)slot].stagedReady.load(std::memory_order_acquire);
    }
    static size_t retiredSize(FxRack& r) { return r.retired.size(); }
    static size_t retiredCapacity(FxRack& r) { return r.retired.capacity(); }
    static std::unique_lock<std::mutex> holdRetire(FxRack& r) {
        return std::unique_lock<std::mutex>(r.retireMutex);
    }
    static void setSuspendReq(FxRack& r, int v) {
        r.suspendReq.store(v, std::memory_order_release);
    }
    static long long suspendAck(FxRack& r) {
        return r.suspendAck.load(std::memory_order_acquire);
    }
};

namespace {

juce::AudioPluginInstance* stageStub(FxRack& r, int slot) {
    auto raw = new StubPlugin(); // NOLINT — ownership moves into the slot
    FxRackTestAccess::stage(r, slot, std::unique_ptr<juce::AudioPluginInstance>(raw));
    return raw;
}

void testSuspendImmediate() {
    FxRack rack;
    check(rack.suspendProcessing(),
          "suspend succeeds immediately while audio stopped");
    rack.resumeProcessing();
}

void testSuspendHandshake() {
    FxRack rack;
    // Adopt a working stub first so "silently suspended" and "processing
    // after resume" are both observable (not just an empty slot).
    juce::AudioPluginInstance* p = stageStub(rack, 0);
    rack.blockBoundary();
    check(FxRackTestAccess::active(rack, 0) == p, "adopt before handshake");
    rack.setAudioRunning(true);
    std::atomic<bool> stop{ false };
    std::thread audio([&] {
        while (!stop.load(std::memory_order_acquire)) {
            rack.blockBoundary();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    const bool suspended = rack.suspendProcessing();
    check(suspended, "suspend acked by simulated audio boundary");
    check(FxRackTestAccess::suspendAck(rack) > 0, "suspendAck advanced");
    float l[64], rr[64];
    for (int i = 0; i < 64; ++i) {
        l[i] = 1.0f;
        rr[i] = 1.0f;
    }
    rack.processAux(0, l, rr, 64);
    bool silent = true;
    for (int i = 0; i < 64; ++i)
        if (std::fabs(l[i]) > 1.0e-6f || std::fabs(rr[i]) > 1.0e-6f)
            silent = false;
    check(silent, "processAux silent while suspended");
    rack.resumeProcessing();
    stop.store(true, std::memory_order_release);
    audio.join();
    rack.setAudioRunning(false);
    for (int i = 0; i < 64; ++i) {
        l[i] = 1.0f;
        rr[i] = 1.0f;
    }
    rack.processAux(0, l, rr, 64);
    check(std::fabs(l[0] - 0.5f) < 1.0e-6f && std::fabs(rr[15] - 0.5f) < 1.0e-6f,
          "processing resumes after suspend");
}

void testPickupDeferredWhileSuspended() {
    FxRack rack;
    FxRackTestAccess::setSuspendReq(rack, 1); // message holds a suspend
    juce::AudioPluginInstance* raw = stageStub(rack, 0);
    rack.blockBoundary(); // must ack only — no swap under suspend
    check(FxRackTestAccess::stagedReady(rack, 0), "pickup deferred under suspend");
    check(FxRackTestAccess::active(rack, 0) == nullptr, "no active yet");
    FxRackTestAccess::setSuspendReq(rack, 0);
    rack.blockBoundary(); // normal pickup
    check(!FxRackTestAccess::stagedReady(rack, 0), "pickup after resume");
    check(FxRackTestAccess::active(rack, 0) == raw, "instance active");
    // Prove the active slot processes: pass-through input halved.
    float l[16], rr[16];
    for (int i = 0; i < 16; ++i) {
        l[i] = 1.0f;
        rr[i] = 1.0f;
    }
    rack.processAux(0, l, rr, 16);
    check(std::fabs(l[0] - 0.5f) < 1.0e-6f
              && std::fabs(rr[15] - 0.5f) < 1.0e-6f,
          "active instance processes");
}

void testRetireDeferredByLock() {
    FxRack rack;
    juce::AudioPluginInstance* first = stageStub(rack, 0);
    rack.blockBoundary();
    check(FxRackTestAccess::active(rack, 0) == first, "initial adopt");
    juce::AudioPluginInstance* second = stageStub(rack, 0);
    {
        auto hold = FxRackTestAccess::holdRetire(rack); // retire lock busy
        rack.blockBoundary();
        check(FxRackTestAccess::active(rack, 0) == first,
              "retire deferred while lock held");
        check(FxRackTestAccess::stagedReady(rack, 0),
              "staged survives deferral");
        check(FxRackTestAccess::retiredSize(rack) == 0, "nothing retired yet");
    }
    rack.blockBoundary(); // lock free again: swap + retire
    check(FxRackTestAccess::active(rack, 0) == second, "swap after unlock");
    check(FxRackTestAccess::retiredSize(rack) == 1, "old instance retired");
    const int before = gDestroyed.load(std::memory_order_relaxed);
    rack.pumpRetired(); // message thread destroys it
    check(gDestroyed.load(std::memory_order_relaxed) == before + 1,
          "retired instance destroyed by pump");
}

void testRetireCapacityDeferral() {
    FxRack rack;
    const size_t cap = FxRackTestAccess::retiredCapacity(rack);
    check(cap >= 16, "retire capacity reserved up front");
    juce::AudioPluginInstance* cur = stageStub(rack, 0);
    rack.blockBoundary(); // first adopt: nothing outgoing
    check(FxRackTestAccess::active(rack, 0) == cur, "cold adopt");
    for (size_t i = 0; i < cap; ++i) {
        juce::AudioPluginInstance* next = stageStub(rack, 0);
        rack.blockBoundary();
        if (FxRackTestAccess::active(rack, 0) != next) {
            check(false, "swap fits inside capacity");
            return;
        }
        cur = next;
    }
    check(FxRackTestAccess::retiredSize(rack) == cap, "retired filled to capacity");
    juce::AudioPluginInstance* overflow = stageStub(rack, 0);
    rack.blockBoundary(); // must defer: retired.size() == capacity
    check(FxRackTestAccess::active(rack, 0) == cur, "admission deferred at capacity");
    check(FxRackTestAccess::stagedReady(rack, 0), "overflow staged stays ready");
    check(FxRackTestAccess::retiredSize(rack) == cap, "no push beyond capacity");
    const int before = gDestroyed.load(std::memory_order_relaxed);
    rack.pumpRetired();
    check(gDestroyed.load(std::memory_order_relaxed) == before + (int)cap,
          "pump frees the full retired set");
    rack.blockBoundary(); // capacity free: final adopt completes
    check(FxRackTestAccess::active(rack, 0) == overflow, "final adopt after pump");
    check(FxRackTestAccess::retiredSize(rack) == 1, "latest old retired");
}

int run() {
    testSuspendImmediate();
    testSuspendHandshake();
    testPickupDeferredWhileSuspended();
    testRetireDeferredByLock();
    testRetireCapacityDeferral();
    if (gCreated.load(std::memory_order_relaxed)
        == gDestroyed.load(std::memory_order_relaxed))
        check(true, "every stub destroyed exactly once");
    else
        check(false, "every stub destroyed exactly once");
    if (failures == 0)
        std::printf("FXRACK_TEST PASS\n");
    else
        std::printf("FXRACK_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
