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

    // -- audio thread --
    void processAux(int aux, float* left, float* right, int numSamples) override;
    void adoptStaged(); // pickup staged instances at a block boundary

private:
    struct Slot {
        std::unique_ptr<juce::AudioPluginInstance> active; // audio thread only
        std::unique_ptr<juce::AudioPluginInstance> staged; // guarded below
        std::atomic<bool> stagedReady{ false };
        std::atomic<bool> bypass{ false };
        juce::String name = "-";
    };

    std::array<Slot, kSlots> slots;
    std::mutex stageMutex;
    std::mutex retireMutex;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> retired;
    // Overflow for the near-impossible case where the retire lock is held
    // exactly when the audio thread needs it; replacing destroys the previous
    // occupant on the audio thread (bounded, documented, effectively unreachable
    // since pumpRetired destroys outside the lock).
    std::unique_ptr<juce::AudioPluginInstance> overflow;
    std::array<std::unique_ptr<juce::DialogWindow>, kSlots> editors;
    std::array<juce::String, kSlots> paths;
    juce::MidiBuffer scratchMidi;
    std::atomic<bool> rateDirty{ false };
    double rate = 44100.0;
    int blockSize = 1024;
};
