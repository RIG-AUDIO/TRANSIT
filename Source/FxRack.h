// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Mixer.h"

// VST3 FX rack: one hosted insert per aux bus. Message thread owns plugin
// lifetime (load/unload/prepare/editors); the audio thread only swaps staged
// instances and calls processBlock. Bypassed/empty/unprepared slots report
// silence (tails cut — documented in the README).
//
// Concurrency protocol (three rules):
//  1. `staged` is only touched under stageMutex; the audio thread picks it
//     up with try_lock (never blocks) — see adoptStaged().
//  2. `active` is only touched by the audio thread, EXCEPT while the message
//     thread holds a suspend: suspendProcessing() sets a request the audio
//     thread acknowledges at the next block boundary, after which every
//     processAux call outputs silence without touching plugin state. Used
//     around prepareToPlay and editor creation.
//  3. The audio thread never allocates or destroys plugin objects: outgoing
//     instances move into `retired` only when the retire lock is free AND
//     the vector has spare capacity; otherwise the whole swap defers to a
//     later block. pumpRetired() destroys them on the message thread.
class FxRack : public lean::AuxHost {
public:
    static constexpr int kSlots = lean::kAuxBuses;

    FxRack();
    ~FxRack() override;

    // -- message thread --
    juce::StringArray scanVst3(); // bundle display names, no loading
    bool loadPlugin(int slot, const juce::String& bundlePath, juce::String& error);
    void unloadPlugin(int slot);
    void setBypass(int slot, bool bypassed);
    bool isBypassed(int slot) const;
    juce::String slotName(int slot) const;
    juce::String slotPath(int slot) const;
    bool hasEditor(int slot) const;
    void showEditor(int slot);
    void prepare(double sampleRate, int blockSize);
    void pumpRetired(); // destroy retired instances (message thread)
    void setRateDirty(bool dirty) { rateDirty.store(dirty); }
    bool isRateDirty() const { return rateDirty.load(); }
    // Device-callback liveness (AboutToStart/Stopped). When no callback can
    // run, suspendProcessing succeeds immediately.
    void setAudioRunning(bool running);
    // Suspend aux processing so plugin state can be touched safely. Waits
    // (bounded, message thread only) for the audio acknowledgement; false on
    // timeout — the caller must abort the operation. Audio stays silent for
    // the suspended period (retry via rateDirty/timer when prepare fails).
    bool suspendProcessing();
    void resumeProcessing();

    // -- audio thread --
    void processAux(int aux, float* left, float* right, int numSamples) override;
    void blockBoundary() override; // suspend ack + staged pickup, every block
    void adoptStaged(); // pickup staged instances at a block boundary

private:
    // In-process protocol test seam (review C4): stages stub instances and
    // inspects slot/retire/handshake state without loading VST3 files.
    friend struct FxRackTestAccess;

    struct Slot {
        std::unique_ptr<juce::AudioPluginInstance> active; // audio thread only
        std::unique_ptr<juce::AudioPluginInstance> staged; // guarded below
        std::atomic<bool> stagedReady{ false };
        std::atomic<bool> bypass{ false };
        juce::String name = "-";
    };

    // Editor teardown touches the instance: suspend first. False when the
    // audio thread does not acknowledge in time — the caller aborts.
    bool closeSlotEditor(int slot);
    // Full unload (editor + staged + metadata). False when aborted by a
    // suspend timeout; the slot is left untouched for a retry.
    bool tryUnloadPlugin(int slot);

    static constexpr size_t kRetireReserve = 16;

    std::array<Slot, kSlots> slots;
    std::mutex stageMutex;
    std::mutex retireMutex;
    // Only ever pushed to from adoptStaged() with spare capacity (no
    // reallocation) and drained by pumpRetired() on the message thread.
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> retired;
    std::array<std::unique_ptr<juce::DialogWindow>, kSlots> editors;
    std::array<juce::String, kSlots> paths;
    juce::MidiBuffer scratchMidi;
    std::atomic<bool> rateDirty{ false };
    // Suspend handshake: 0 = running, 1 = message requests suspend; audio
    // increments suspendAck at each block boundary that observes the request.
    std::atomic<bool> audioActive{ false };
    std::atomic<int> suspendReq{ 0 };
    std::atomic<long long> suspendAck{ 0 };
    double rate = 44100.0;
    int blockSize = 1024;
};
