// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "AudioEngine.h"

#include "Decode.h"

#include <algorithm>
#include <cmath>

AudioEngine::AudioEngine() {
    deviceManager.initialiseWithDefaultDevices(0, 2);
    // Prefer 4 outputs (master 1/2 + phones 3/4); fall back to stereo.
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    applySetup(setup);
    mixer.setAuxHost(&fxRack);
    if (auto* device = deviceManager.getCurrentAudioDevice())
        fxRack.prepare(device->getCurrentSampleRate(), 1024);
    deviceManager.addAudioCallback(this);
}

juce::String AudioEngine::applySetup(
    juce::AudioDeviceManager::AudioDeviceSetup setup) {
    setup.outputChannels.clear();
    setup.outputChannels.setRange(0, 4, true);
    setup.useDefaultOutputChannels = false;
    juce::String error = deviceManager.setAudioDeviceSetup(setup, true);
    fourOuts = error.isEmpty();
    if (!fourOuts) {
        // Device can't do 4 outs: retry with its default stereo config.
        deviceManager.getAudioDeviceSetup(setup);
        setup.useDefaultOutputChannels = true;
        error = deviceManager.setAudioDeviceSetup(setup, true);
    }
    if (auto* device = deviceManager.getCurrentAudioDevice())
        mixer.setOutputRate(device->getCurrentSampleRate());
    return error;
}

juce::StringArray AudioEngine::audioDeviceTypes() {
    juce::StringArray names;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        names.add(type->getTypeName());
    return names;
}

juce::String AudioEngine::currentAudioDeviceType() {
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        return type->getTypeName();
    return {};
}

void AudioEngine::setAudioDeviceType(const juce::String& type) {
    deviceManager.setCurrentAudioDeviceType(type, true);
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    applySetup(setup);
}

juce::StringArray AudioEngine::outputDeviceNames() {
    juce::StringArray names;
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        names = type->getDeviceNames(false);
    return names;
}

juce::String AudioEngine::currentOutputDevice() const {
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    return setup.outputDeviceName;
}

juce::String AudioEngine::setOutputDevice(const juce::String& name) {
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    setup.outputDeviceName = name;
    return applySetup(setup);
}

juce::Array<int> AudioEngine::availableSampleRates() {
    juce::Array<int> rates;
    if (auto* device = deviceManager.getCurrentAudioDevice())
        for (double r : device->getAvailableSampleRates())
            rates.add((int)std::lround(r));
    return rates;
}

juce::String AudioEngine::setSampleRate(double rate) {
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    setup.sampleRate = rate;
    return applySetup(setup);
}

juce::Array<int> AudioEngine::availableBufferSizes() {
    juce::Array<int> sizes;
    if (auto* device = deviceManager.getCurrentAudioDevice())
        sizes = device->getAvailableBufferSizes();
    return sizes;
}

juce::String AudioEngine::setBufferSize(int samples) {
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    setup.bufferSize = samples;
    return applySetup(setup);
}

void AudioEngine::setKeylockEco(bool eco) {
    keylockEcoState = eco;
    for (int d = 0; d < 2; ++d)
        mixer.channel(d).deck.setKeylockEco(eco);
}

int AudioEngine::outputLatencySamples() const {
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getOutputLatencyInSamples();
    return 0;
}

double AudioEngine::currentSampleRate() const {
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();
    return 0.0;
}

int AudioEngine::currentBufferSize() const {
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();
    return 0;
}

bool AudioEngine::clearLibrary() {
    ensureDb();
    return library.isOpen() && library.clear();
}

AudioEngine::~AudioEngine() {
    deviceManager.removeAudioCallback(this);
    midiIn.reset();
}

juce::StringArray AudioEngine::midiInputNames() const {
    juce::StringArray names;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        names.add(d.name);
    return names;
}

bool AudioEngine::openMidiInput(int index) {
    midiIn.reset();
    const auto devs = juce::MidiInput::getAvailableDevices();
    if (index < 0 || index >= devs.size())
        return index < 0; // closing is always "ok"
    midiIn = juce::MidiInput::openDevice(devs[index].identifier, this);
    if (!midiIn)
        return false;
    midiIn->start();
    return true;
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*,
                                            const juce::MidiMessage& message) {
    // MIDI thread: stamp with the audio frame counter and queue. No UI, no
    // locks; the audio thread drains and applies at the next block.
    if (message.isController()) {
        midiQueue.push(frameCount.load(std::memory_order_relaxed),
                       message.getChannel(), message.getControllerNumber(),
                       (float)message.getControllerValue() / 127.0f);
        learnCh.store(message.getChannel(), std::memory_order_relaxed);
        learnCc.store(message.getControllerNumber(), std::memory_order_relaxed);
        learnStamp.fetch_add(1, std::memory_order_relaxed);
    }
}

void AudioEngine::applyMidiEvent(const lean::MidiEvent& ev) {
    const int code = midiGrid.lookup(ev.channel, ev.cc);
    if (code < 0)
        return;
    const int deck = lean::MidiGrid::gridDeck(code);
    const auto target = lean::MidiGrid::gridTarget(code);
    const float v = lean::midiValue(target, ev.value01);
    switch (target) {
        case lean::MidiTarget::Tempo:
            mixer.channel(deck).deck.setTempo((double)v);
            break;
        case lean::MidiTarget::Gain:
            mixer.channel(deck).deck.setGain(v);
            break;
        case lean::MidiTarget::Fader: mixer.setFader(deck, v); break;
        case lean::MidiTarget::Low:
        case lean::MidiTarget::Mid:
        case lean::MidiTarget::High: {
            double lo = 0.0, mi = 0.0, hi = 0.0;
            mixer.eqGains(deck, lo, mi, hi);
            if (target == lean::MidiTarget::Low)
                lo = v;
            else if (target == lean::MidiTarget::Mid)
                mi = v;
            else
                hi = v;
            mixer.setEq(deck, lo, mi, hi);
            break;
        }
        case lean::MidiTarget::Xfade: mixer.setCrossfader(v); break;
        case lean::MidiTarget::Master: mixer.setMaster(v); break;
        case lean::MidiTarget::Blend: mixer.setCueBlend(v); break;
        case lean::MidiTarget::Phones: mixer.setPhonesLevel(v); break;
        case lean::MidiTarget::None: break;
    }
}

float AudioEngine::getFader(int deck) const {
    return mixer.faderLevel(deck & 1);
}

float AudioEngine::getMaster() const {
    return mixer.masterLevel();
}

float AudioEngine::getPhonesLevel() const {
    return mixer.phonesLevelValue();
}

float AudioEngine::getXfade() const {
    return mixer.xfadeValue();
}

float AudioEngine::getBlend() const {
    return mixer.blendValue();
}

float AudioEngine::getGain(int deck) const {
    return mixer.channel(deck & 1).deck.gainTarget();
}

void AudioEngine::getEq(int deck, double& lo, double& mi, double& hi) const {
    mixer.eqGains(deck & 1, lo, mi, hi);
}

void AudioEngine::setSend(int deck, int aux, float v) {
    mixer.setSend(deck & 1, aux, v);
}

void AudioEngine::setAuxReturn(int aux, float v) {
    mixer.setAuxReturn(aux, v);
}

float AudioEngine::getSend(int deck, int aux) const {
    return mixer.sendLevel(deck & 1, aux);
}

float AudioEngine::getAuxReturn(int aux) const {
    return mixer.returnLevel(aux);
}

bool AudioEngine::toggleRecord(juce::String& status) {    if (recorder.isRecording()) {
        recorder.stop();
        status = "Recorded, drops: " + juce::String(recorder.droppedFrames());
        return true;
    }
    auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                   .getChildFile("TRANSIT");
    dir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto file = dir.getChildFile("TRANSIT-" + stamp + ".wav");
    double rate = 48000.0;
    if (auto* device = deviceManager.getCurrentAudioDevice())
        rate = device->getCurrentSampleRate();
    if (!recorder.start(file.getFullPathName().toStdString(), rate)) {
        status = "Record failed to start";
        return false;
    }
    status = "Recording: " + file.getFileName();
    return true;
}

bool AudioEngine::loadFile(int deck, const juce::String& path, juce::String& error) {
    deck &= 1;
    lean::DecodedAudio decoded;
    std::string err;
    if (!lean::decodeFile(path.toStdString(), decoded, err)) {
        error = juce::String(err);
        return false;
    }
    const int frames = (int)(decoded.stereo.size() / 2);
    const int target = std::clamp((int)(frames / decoded.sampleRate * 8.0), 256, 8192);
    waves[deck] = lean::computePeaks(decoded.stereo.data(), frames,
                                     decoded.sampleRate, target);
    bpm[deck] = lean::detectBpm(decoded.stereo.data(), frames, decoded.sampleRate);
    mixer.channel(deck).deck.load(std::move(decoded.stereo), decoded.sampleRate);
    if (auto* device = deviceManager.getCurrentAudioDevice())
        mixer.setOutputRate(device->getCurrentSampleRate());
    mixer.reset();
    fileLoaded[deck] = true;
    return true;
}

float AudioEngine::takePeak() {
    float master = 0.0f, a = 0.0f, b = 0.0f;
    takePeaks(master, a, b);
    return master;
}

void AudioEngine::takePeaks(float& master, float& chA, float& chB) {
    int start1, size1, start2, size2;
    peakFifo.prepareToRead(peakFifo.getNumReady(), start1, size1, start2, size2);
    master = chA = chB = 0.0f;
    for (int i = 0; i < size1; ++i) {
        master = std::max(master, peakSlots[start1 + i].master);
        chA = std::max(chA, peakSlots[start1 + i].chA);
        chB = std::max(chB, peakSlots[start1 + i].chB);
    }
    for (int i = 0; i < size2; ++i) {
        master = std::max(master, peakSlots[start2 + i].master);
        chA = std::max(chA, peakSlots[start2 + i].chA);
        chB = std::max(chB, peakSlots[start2 + i].chB);
    }
    peakFifo.finishedRead(size1 + size2);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device) {
        fxRack.setAudioRunning(true); // suspend handshake may begin now
        mixer.setOutputRate(device->getCurrentSampleRate());
        deviceRate = device->getCurrentSampleRate();
        fxRack.setRateDirty(true); // audio skips aux until message prepares
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*, int,
                                                  float* const* outputChannelData,
                                                  int numOutputChannels,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&) {
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch])
            std::fill(outputChannelData[ch], outputChannelData[ch] + numSamples, 0.0f);

    float* left = numOutputChannels > 0 ? outputChannelData[0] : nullptr;
    float* right = numOutputChannels > 1 ? outputChannelData[1] : left;
    float* phL = numOutputChannels > 3 ? outputChannelData[2] : nullptr;
    float* phR = numOutputChannels > 3 ? outputChannelData[3] : nullptr;

    // Drain timestamped MIDI first: control latency is bounded by one device
    // block, with no message-thread jitter in the path.
    lean::MidiEvent events[32];
    const int nEv =
        midiQueue.drain(blockFrame + (uint64_t)numSamples, events, 32);
    for (int i = 0; i < nEv; ++i)
        applyMidiEvent(events[i]);

    const int64_t t0 = juce::Time::getHighResolutionTicks();
    if (left)
        mixer.render(left, right ? right : left, phL, phR, numSamples);
    const int64_t t1 = juce::Time::getHighResolutionTicks();
    blockFrame += (uint64_t)numSamples;
    // Publish the audio frame counter for MIDI stamping. Events stamped with
    // the value observed before this store apply at the next block boundary;
    // the counter is monotonic, so the queue can never see a regression.
    frameCount.store(blockFrame, std::memory_order_relaxed);

    // Render watchdog: worst-case time and overruns past the block budget.
    const double ticksPerSec =
        (double)juce::Time::getHighResolutionTicksPerSecond();
    if (ticksPerSec > 0.0 && deviceRate > 0.0) {
        const long long us =
            (long long)((double)(t1 - t0) * 1000000.0 / ticksPerSec);
        long long prev = maxRenderUsUs.load(std::memory_order_relaxed);
        while (us > prev
               && !maxRenderUsUs.compare_exchange_weak(prev, us,
                                                      std::memory_order_relaxed)) {
        }
        const long long budget =
            (long long)((double)numSamples * 1000000.0 / deviceRate);
        if (us > budget)
            overruns.fetch_add(1, std::memory_order_relaxed);
    }

    if (left && recorder.isRecording())
        recorder.pushBlock(left, right ? right : left, numSamples);

    float peak = 0.0f;
    if (left) {
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(left[i]));
        if (right && right != left)
            for (int i = 0; i < numSamples; ++i)
                peak = std::max(peak, std::abs(right[i]));
    }

    int start1, size1, start2, size2;
    peakFifo.prepareToWrite(1, start1, size1, start2, size2);
    const int slot = size1 > 0 ? start1 : (size2 > 0 ? start2 : -1);
    if (slot >= 0) {
        peakSlots[slot].master = peak;
        peakSlots[slot].chA = mixer.channelPeak(0);
        peakSlots[slot].chB = mixer.channelPeak(1);
    }
    peakFifo.finishedWrite(slot >= 0 ? 1 : 0);
}
