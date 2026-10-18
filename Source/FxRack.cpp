// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "FxRack.h"

#include <chrono>
#include <thread>

FxRack::FxRack() {
    retired.reserve(kRetireReserve); // capacity for the no-realloc push path
}

FxRack::~FxRack() {
    for (auto& e : editors)
        e.reset();
}

juce::StringArray FxRack::scanVst3() {
    juce::StringArray names;
    juce::VST3PluginFormat format;
#ifdef _WIN32
    juce::FileSearchPath searchPaths(
        "C:\\Program Files\\Common Files\\VST3;C:\\Program Files (x86)\\Common Files\\VST3");
#else
    // Linux-first bundle locations (user, system, distro multiarch) plus the
    // conventional VST3_PATH override (colon-separated, GNU/POSIX style).
    juce::String entries;
    auto add = [&entries](const juce::String& s) {
        if (s.isEmpty())
            return;
        if (entries.isNotEmpty())
            entries << ':';
        entries << s;
    };
    add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile(".vst3")
            .getFullPathName());
    add("/usr/lib/vst3");
    add("/usr/local/lib/vst3");
    add("/usr/lib64/vst3");
    add("/usr/lib/x86_64-linux-gnu/vst3");
    add("/usr/lib/aarch64-linux-gnu/vst3");
#ifdef __APPLE__
    add("/Library/Audio/Plug-Ins/VST3");
    add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Plug-Ins/VST3")
            .getFullPathName());
#endif
    const auto extra =
        juce::SystemStats::getEnvironmentVariable("VST3_PATH", juce::String());
    for (const auto& e : juce::StringArray::fromTokens(extra, ":", ""))
        if (e.isNotEmpty())
            add(e.trim());
    juce::FileSearchPath searchPaths(entries);
#endif
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
    if (!tryUnloadPlugin(slot)) {
        error = "Audio engine busy - retry";
        return false;
    }

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

bool FxRack::closeSlotEditor(int slot) {
    if (editors[(size_t)slot] == nullptr)
        return true; // nothing to tear down: no suspend needed
    // Closing an editor touches the instance it belongs to: hold a suspend
    // so the audio thread is in its silence branch first.
    if (!suspendProcessing())
        return false; // audio busy: caller aborts, retry later
    editors[(size_t)slot].reset();
    resumeProcessing();
    return true;
}

bool FxRack::tryUnloadPlugin(int slot) {
    if (!closeSlotEditor(slot))
        return false;
    paths[(size_t)slot].clear();
    {
        std::lock_guard<std::mutex> lock(stageMutex);
        slots[(size_t)slot].staged.reset();
        slots[(size_t)slot].stagedReady.store(true, std::memory_order_release);
    }
    slots[(size_t)slot].name = "-";
    return true;
}

void FxRack::unloadPlugin(int slot) {
    if (slot < 0 || slot >= kSlots)
        return;
    (void)tryUnloadPlugin(slot); // busy: leave for a retry; UI keeps state
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
    // Editor creation touches the active instance: suspend first. The audio
    // thread acknowledges at the next block boundary and stays silent
    // throughout, so createEditorAndMakeActive cannot race processBlock.
    if (!suspendProcessing())
        return; // timeout: audio did not acknowledge; caller may retry
    juce::AudioPluginInstance* target = slots[(size_t)slot].active.get();
    if (target && target->hasEditor()) {
        if (auto* ed = target->createEditorAndMakeActive()) {
            juce::DialogWindow::LaunchOptions opts;
            opts.content.setOwned(ed);
            opts.dialogTitle = target->getName();
            opts.useNativeTitleBar = true;
            opts.resizable = true;
            resumeProcessing();
            window.reset(opts.launchAsync());
            return;
        }
    }
    resumeProcessing();
}

void FxRack::prepare(double sampleRate, int block) {
    const double newRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int newBlock = block > 0 ? block : 1024;
    {
        // rate/blockSize/scratchMidi preparation and staged instances: staged
        // is only visible to audio under stageMutex, so the lock suffices.
        std::lock_guard<std::mutex> lock(stageMutex);
        rate = newRate;
        blockSize = newBlock;
        for (auto& s : slots)
            if (s.staged)
                s.staged->prepareToPlay(rate, blockSize);
    }
    // Active instances are the audio thread's: suspend around prepareToPlay.
    // On timeout keep rateDirty set — the UI timer retries every tick and the
    // audio side keeps aux muted, so a stale prepare is never processed.
    if (suspendProcessing()) {
        scratchMidi.clear(); // pre-touch storage while audio is silent
        for (auto& s : slots)
            if (s.active)
                s.active->prepareToPlay(rate, blockSize);
        resumeProcessing();
        rateDirty.store(false, std::memory_order_release);
    }
}

void FxRack::pumpRetired() {
    // Swap out under the lock, destroy after unlock: the audio thread's
    // try_lock only ever contends with pointer moves, never destruction.
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> dead;
    {
        std::lock_guard<std::mutex> lock(retireMutex);
        dead.swap(retired);
        retired.reserve(kRetireReserve); // swap took the capacity back
    }
}

void FxRack::setAudioRunning(bool running) {
    audioActive.store(running, std::memory_order_release);
    if (!running)
        suspendReq.store(0, std::memory_order_release);
}

bool FxRack::suspendProcessing() {
    if (!audioActive.load(std::memory_order_acquire)) {
        // No device callback can be in flight (not started, or stopped
        // before we looked): plugin state is already ours to touch.
        suspendReq.store(0, std::memory_order_release);
        return true;
    }
    const long long before = suspendAck.load(std::memory_order_acquire);
    suspendReq.store(1, std::memory_order_release);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    for (;;) {
        if (suspendAck.load(std::memory_order_acquire) > before)
            return true; // audio observed the request; it stays silent now
        if (!audioActive.load(std::memory_order_acquire)) {
            // Callbacks stopped while we waited: nothing can run anymore.
            suspendReq.store(0, std::memory_order_release);
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    suspendReq.store(0, std::memory_order_release);
    return false; // caller aborts; the timer retries on the next tick
}

void FxRack::resumeProcessing() {
    suspendReq.store(0, std::memory_order_release);
}

void FxRack::blockBoundary() {
    if (suspendReq.load(std::memory_order_acquire) != 0) {
        // Acknowledge once per block while requested, and do NOT swap slots:
        // the message thread may be preparing the active instance right now.
        suspendAck.fetch_add(1, std::memory_order_release);
        return;
    }
    adoptStaged(); // pickup runs every block, even when all sends are silent
}

void FxRack::adoptStaged() {
    for (auto& s : slots) {
        if (!s.stagedReady.load(std::memory_order_acquire))
            continue;
        if (!stageMutex.try_lock())
            break; // message thread is staging; retry next block, never block
        // Swap only when the outgoing instance can be handed over without
        // allocating (spare capacity) or destroying (lock held long enough
        // for pumpRetired) on this thread. Otherwise defer: stagedReady stays
        // set and the next block tries again. The audio thread therefore
        // never allocates, frees, or destroys plugin objects.
        bool canSwap = true;
        if (s.active) {
            if (!retireMutex.try_lock()) {
                canSwap = false;
            } else if (retired.size() >= retired.capacity()) {
                retireMutex.unlock();
                canSwap = false;
            } else {
                // Move only: constructs a slot (no realloc under capacity)
                // and leaves s.active null. Nothing is destroyed here.
                retired.push_back(std::move(s.active));
                retireMutex.unlock();
            }
        }
        if (canSwap) {
            s.active = std::move(s.staged);
            s.stagedReady.store(false, std::memory_order_release);
        }
        stageMutex.unlock();
        if (!canSwap)
            break; // retry next block
    }
}

void FxRack::processAux(int aux, float* left, float* right, int numSamples) {
    if (aux < 0 || aux >= kSlots || !left || !right || numSamples <= 0)
        return;
    if (suspendReq.load(std::memory_order_acquire) != 0) {
        // Message thread holds a suspend: never touch plugin state here.
        for (int i = 0; i < numSamples; ++i)
            left[i] = right[i] = 0.0f;
        return;
    }
    adoptStaged(); // idempotent; also covers direct calls (dev probes)
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
