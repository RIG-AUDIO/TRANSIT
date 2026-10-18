// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "FxRack.h"

FxRack::FxRack() {
    retired.reserve(16); // pre-touch: audio-side push_back never reallocates
}

FxRack::~FxRack() {
    for (auto& e : editors)
        e.reset();
}

juce::StringArray FxRack::scanVst3() {
    juce::StringArray names;
    juce::VST3PluginFormat format;
    juce::FileSearchPath searchPaths(
        "C:\\Program Files\\Common Files\\VST3;C:\\Program Files (x86)\\Common Files\\VST3");
    for (int i = 0; i < searchPaths.getNumPaths(); ++i) {
        juce::Array<juce::File> found;
        searchPaths[i].findChildFiles(found, juce::File::findFilesAndDirectories,
                                      false, "*.vst3");
        for (const auto& f : found)
            names.add(f.getFileNameWithoutExtension() + "|" + f.getFullPathName());
    }
    names.sort(true);
    return names;
}

bool FxRack::loadPlugin(int slot, const juce::String& bundlePath,
                        juce::String& error) {
    if (slot < 0 || slot >= kSlots)
        return false;
    unloadPlugin(slot);

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, bundlePath);
    if (found.isEmpty()) {
        error = "No VST3 found in bundle";
        return false;
    }
    juce::String err;
    auto instance =
        format.createInstanceFromDescription(*found[0], rate, blockSize, err);
    if (!instance) {
        error = err.isNotEmpty() ? err : "Instance creation failed";
        return false;
    }
    instance->prepareToPlay(rate, blockSize);
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        slots[(size_t)slot].staged = std::move(instance);
    }
    slots[(size_t)slot].stagedReady.store(true, std::memory_order_release);
    slots[(size_t)slot].name = found[0]->name;
    paths[(size_t)slot] = bundlePath;
    return true;
}

void FxRack::unloadPlugin(int slot) {
    if (slot < 0 || slot >= kSlots)
        return;
    editors[(size_t)slot].reset();
    paths[(size_t)slot].clear();
    std::lock_guard<std::mutex> lock(stageMutex);
    slots[(size_t)slot].staged.reset();
    slots[(size_t)slot].stagedReady.store(true, std::memory_order_release);
    slots[(size_t)slot].name = "-";
}

void FxRack::setBypass(int slot, bool bypassed) {
    if (slot >= 0 && slot < kSlots)
        slots[(size_t)slot].bypass.store(bypassed, std::memory_order_relaxed);
}

bool FxRack::isBypassed(int slot) const {
    if (slot < 0 || slot >= kSlots)
        return true;
    return slots[(size_t)slot].bypass.load(std::memory_order_relaxed);
}

juce::String FxRack::slotName(int slot) const {
    if (slot < 0 || slot >= kSlots)
        return "-";
    return slots[(size_t)slot].name;
}

juce::String FxRack::slotPath(int slot) const {
    if (slot < 0 || slot >= kSlots)
        return {};
    return paths[(size_t)slot];
}

bool FxRack::hasEditor(int slot) const {
    if (slot < 0 || slot >= kSlots)
        return false;
    // Editor presence is only meaningful for the staged/active instance;
    // report true when a plugin name is set (instance exists or incoming).
    return slots[(size_t)slot].name != "-";
}

void FxRack::showEditor(int slot) {
    if (slot < 0 || slot >= kSlots)
        return;
    auto& window = editors[(size_t)slot];
    window.reset();
    // Prefer the active instance; editors are message-thread only.
    juce::AudioPluginInstance* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        // Active lives on the audio thread; read under the staging lock is
        // not synchronized with audio use, so only open editors for settled
        // (non-pending) slots. Pending swaps complete within one block.
        if (!slots[(size_t)slot].stagedReady.load(std::memory_order_acquire))
            target = slots[(size_t)slot].active.get();
    }
    if (!target || !target->hasEditor())
        return;
    if (auto* ed = target->createEditorAndMakeActive()) {
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(ed);
        opts.dialogTitle = target->getName();
        opts.useNativeTitleBar = true;
        opts.resizable = true;
        window.reset(opts.launchAsync());
    }
}

void FxRack::prepare(double sampleRate, int block) {
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    blockSize = block > 0 ? block : 1024;
    scratchMidi.clear(); // pre-touch storage on the message thread
    std::lock_guard<std::mutex> lock(stageMutex);
    for (auto& s : slots) {
        if (s.active)
            s.active->prepareToPlay(rate, blockSize);
        if (s.staged)
            s.staged->prepareToPlay(rate, blockSize);
    }
    rateDirty.store(false, std::memory_order_release);
}

void FxRack::pumpRetired() {
    // Swap out under the lock, destroy after unlock: the audio thread's
    // try_lock only ever contends with pointer moves, never destruction.
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> dead;
    {
        std::lock_guard<std::mutex> lock(retireMutex);
        dead.swap(retired);
    }
}

void FxRack::adoptStaged() {
    for (auto& s : slots) {
        if (!s.stagedReady.load(std::memory_order_acquire))
            continue;
        if (!stageMutex.try_lock())
            break; // message thread is staging; retry next block
        if (s.active) {
            if (retireMutex.try_lock()) {
                retired.push_back(std::move(s.active));
                retireMutex.unlock();
            } else {
                overflow = std::move(s.active);
            }
        }
        s.active = std::move(s.staged);
        s.stagedReady.store(false, std::memory_order_release);
        stageMutex.unlock();
    }
}

void FxRack::processAux(int aux, float* left, float* right, int numSamples) {
    if (aux < 0 || aux >= kSlots || !left || !right || numSamples <= 0)
        return;
    adoptStaged();
    auto& s = slots[(size_t)aux];
    if (!s.active || s.bypass.load(std::memory_order_relaxed)
        || rateDirty.load(std::memory_order_relaxed)) {
        for (int i = 0; i < numSamples; ++i)
            left[i] = right[i] = 0.0f;
        return;
    }
    float* channels[2] = { left, right };
    juce::AudioBuffer<float> buf(channels, 2, numSamples);
    scratchMidi.clear();
    s.active->processBlock(buf, scratchMidi);
}
