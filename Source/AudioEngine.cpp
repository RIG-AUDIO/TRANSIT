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
    worker = std::thread(&AudioEngine::workerLoop, this);
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
    juce::StringArray list;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        list.add(type->getTypeName());
    return list;
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
    juce::StringArray list;
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        list = type->getDeviceNames(false);
    return list;
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
    cancelPendingUpdate(); // no commits may land after teardown starts
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        quitting = true;
    }
    jobCv.notify_all();
    if (worker.joinable())
        worker.join(); // finish the in-flight decode before members die
    deviceManager.removeAudioCallback(this);
    midiIn.reset();
}

juce::StringArray AudioEngine::midiInputNames() const {
    juce::StringArray list;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        list.add(d.name);
    return list;
}

bool AudioEngine::openMidiInput(int index) {
    midiIn.reset();
    midiOut.reset();
    const auto devs = juce::MidiInput::getAvailableDevices();
    if (index < 0 || index >= devs.size())
        return index < 0; // closing is always "ok"
    midiIn = juce::MidiInput::openDevice(devs[index].identifier, this);
    if (!midiIn)
        return false;
    // Best-effort output companion for LED echo / SysEx resync (FLX4); a
    // failed open only disables feedback, never the input path.
    midiOut = juce::MidiOutput::openDevice(devs[index].identifier);
    midiIn->start();
    return true;
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*,
                                            const juce::MidiMessage& message) {
    // MIDI thread: stamp with the audio frame counter and queue. No UI, no
    // locks; the audio thread drains and applies at the next block.
    if (message.isController()) {
        const int ch = message.getChannel();
        const int num = message.getControllerNumber();
        const int raw = message.getControllerValue();
        const bool flx4 = flx4Active.load(std::memory_order_seq_cst);
        int v14 = -1;
        int msbNum = num;
        if (flx4)
            v14 = midiGrid.ingest14(ch, num, raw, &msbNum);
        if (v14 >= 0)
            // Queue under the bound (MSB) CC so the audio thread's cell
            // lookup also finds LSB-only fine steps.
            midiQueue.push(frameCount.load(std::memory_order_seq_cst), ch,
                           msbNum, (float)v14 / 16383.0f);
        else
            midiQueue.push(frameCount.load(std::memory_order_seq_cst), ch, num,
                           (float)raw / 127.0f);
        // FLX4 relative encoders (jog, browse) route to the action queue.
        if (flx4) {
            if (const auto* b = lean::flx4FindAction(0, ch, num)) {
                flx4Queue.push(lean::Flx4ActionMsg{
                    (std::uint8_t)b->action, (std::uint8_t)b->deck, 1,
                    (std::int8_t)lean::flx4Relative((std::uint8_t)raw) });
            }
        }
        learnCh.store(ch, std::memory_order_seq_cst);
        learnCc.store(num, std::memory_order_seq_cst);
        learnStamp.fetch_add(1, std::memory_order_seq_cst);
    } else if (flx4Active.load(std::memory_order_seq_cst)
               && (message.isNoteOn() || message.isNoteOff())) {
        // FLX4 transport/PFL/load/browse-press notes -> message-thread
        // action queue (buttons need the UI in the loop).
        if (const auto* b = lean::flx4FindAction(1, message.getChannel(),
                                                 message.getNoteNumber())) {
            const bool on = message.isNoteOn();
            flx4Queue.push(lean::Flx4ActionMsg{
                (std::uint8_t)b->action, (std::uint8_t)b->deck,
                (std::uint8_t)(on ? 1 : 0), 0 });
        }
    }
}

void AudioEngine::applyMidiEvent(const lean::MidiEvent& ev) {
    const int code = midiGrid.lookup(ev.channel, ev.cc);
    if (code < 0)
        return;
    const int deck = lean::MidiGrid::gridDeck(code);
    const auto target = lean::MidiGrid::gridTarget(code);
    // Soft takeover while the FLX4 map is live: the hardware is silent
    // until a control is moved, so the first gesture must not jump the app
    // value. Engage when the hardware reaches or crosses the app value.
    float v01 = ev.value01;
    if (flx4Active.load(std::memory_order_seq_cst)
        && (target == lean::MidiTarget::Tempo || target == lean::MidiTarget::Gain
            || target == lean::MidiTarget::Fader || target == lean::MidiTarget::Low
            || target == lean::MidiTarget::Mid || target == lean::MidiTarget::High
            || target == lean::MidiTarget::Xfade || target == lean::MidiTarget::Blend
            || target == lean::MidiTarget::Send)) {
        // Center-detented hardware knobs (trim, EQ): the detent sends 0.5
        // but the app's detent for those targets sits at 0.8 normalized
        // (unity gain / 0 dB) — remap before pickup so the first touch
        // lands on the app value, not -9 dB. EQ knobs are inverted (low at
        // bottom, high at top) so flip them first.
        float hw = v01;
        if (lean::flx4TargetNeedsInvert(target))
            hw = 1.0f - hw;
        if (lean::flx4TargetHasDetent(target))
            hw = lean::flx4DetentToApp(hw);
        const int cell = (deck & 1) * 16 + (int)target;
        const float picked
            = pickups.process(cell, currentTarget01(target, deck), hw);
        if (picked < 0.0f)
            return;
        v01 = picked;
    }
    const float v = lean::midiValue(target, v01);
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
        case lean::MidiTarget::Send: mixer.setSend(deck, 0, v); break;
        case lean::MidiTarget::None: break;
    }
}

float AudioEngine::currentTarget01(lean::MidiTarget target, int deck) const {
    deck &= 1;
    switch (target) {
        case lean::MidiTarget::Tempo: {
            const double t = mixer.channel(deck).deck.getTempo();
            const double u = (t - 0.5) / 1.0;
            return (float)(u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u));
        }
        case lean::MidiTarget::Gain:
            return mixer.channel(deck).deck.gainTarget() / 1.25f;
        case lean::MidiTarget::Fader: return mixer.faderLevel(deck);
        case lean::MidiTarget::Low:
        case lean::MidiTarget::Mid:
        case lean::MidiTarget::High: {
            double lo = 0.0, mi = 0.0, hi = 0.0;
            mixer.eqGains(deck, lo, mi, hi);
            const double e = target == lean::MidiTarget::Low
                ? lo
                : (target == lean::MidiTarget::Mid ? mi : hi);
            const double u = (e + 24.0) / 30.0;
            return (float)(u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u));
        }
        case lean::MidiTarget::Xfade: return mixer.xfadeValue();
        case lean::MidiTarget::Blend: return mixer.blendValue();
        case lean::MidiTarget::Send: return mixer.sendLevel(deck, 0);
        case lean::MidiTarget::Master: return mixer.masterLevel();
        case lean::MidiTarget::Phones: return mixer.phonesLevelValue();
        case lean::MidiTarget::None: break;
    }
    return 0.0f;
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

void AudioEngine::setFlx4Mode(bool on) {
    flx4Active.store(on, std::memory_order_seq_cst);
    if (!on)
        return;
    int n = 0;
    const lean::Flx4ValueBind* table = lean::flx4ValueTable(n);
    for (int i = 0; i < n; ++i)
        midiGrid.bind14(table[i].ch, table[i].msb, table[i].deck,
                        table[i].target);
}

void AudioEngine::sendFlx4Note(int channel, int note, bool on) {
    if (!midiOut)
        return;
    if (on)
        midiOut->sendMessageNow(
            juce::MidiMessage::noteOn(channel, note, (juce::uint8)0x7F));
    else
        midiOut->sendMessageNow(juce::MidiMessage::noteOff(channel, note));
}

void AudioEngine::sendFlx4SysEx(const unsigned char* bytes, int numBytes) {
    if (!midiOut || bytes == nullptr || numBytes < 4)
        return; // createSysExMessage adds its own F0/F7
    midiOut->sendMessageNow(
        juce::MidiMessage::createSysExMessage(bytes + 1, numBytes - 2));
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

void AudioEngine::loadFile(int deck, const juce::String& path) {
    deck &= 1;
    LoadJob job;
    job.kind = LoadJob::Kind::Load;
    job.deck = deck;
    job.token = gates[deck].submit(); // newest-wins: supersedes any in-flight
    job.path = path.toStdString();
    job.fileName = juce::File(path).getFileName().toStdString();
    states[deck].store((int)lean::LoadState::Loading, std::memory_order_seq_cst);
    errors[deck] = {};
    names[deck] = {};
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        jobs.push_back(std::move(job));
    }
    jobCv.notify_one();
}

void AudioEngine::indexFolder(const juce::String& path) {
    ensureDb();
    if (!library.isOpen())
        return;
    LoadJob job;
    job.kind = LoadJob::Kind::Index;
    job.path = path.toStdString();
    scanning.store(true, std::memory_order_seq_cst);
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        jobs.push_back(std::move(job));
    }
    jobCv.notify_one();
}

void AudioEngine::workerLoop() {
    for (;;) {
        LoadJob job;
        {
            std::unique_lock<std::mutex> lock(jobMutex);
            jobCv.wait(lock, [this] { return quitting || !jobs.empty(); });
            if (jobs.empty()) {
                if (quitting)
                    return;
                continue;
            }
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        if (job.kind == LoadJob::Kind::Index)
            runIndexJob(job);
        else if (job.kind == LoadJob::Kind::ScanFx)
            runScanFxJob();
        else
            runLoadJob(job);
    }
}

void AudioEngine::runIndexJob(const LoadJob& job) {
    int touched = -1;
    if (library.isOpen())
        touched = library.scanFolder(job.path); // Library serializes readers
    lastScanCount.store(touched, std::memory_order_seq_cst);
    // Stay "scanning" while another index job is already queued.
    bool more = false;
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        for (const auto& j : jobs)
            if (j.kind == LoadJob::Kind::Index)
                more = true;
    }
    scanning.store(!more, std::memory_order_seq_cst);
}

void AudioEngine::refreshFxScan() {
    if (fxScanActive.exchange(true, std::memory_order_seq_cst))
        return; // one discovery walk in flight or queued
    LoadJob job;
    job.kind = LoadJob::Kind::ScanFx;
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        jobs.push_back(std::move(job));
    }
    jobCv.notify_one();
}

void AudioEngine::runScanFxJob() {
    // The walk itself is JUCE-free (FxScanPaths) and runs here, off the
    // message thread; root collection uses thread-safe JUCE file/env calls.
    juce::StringArray found = fxRack.scanVst3();
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        fxScanResult = std::move(found);
        fxResultReady = true;
    }
    fxScanActive.store(false, std::memory_order_seq_cst);
    triggerAsyncUpdate(); // commit lands on the message thread
}

void AudioEngine::runLoadJob(const LoadJob& job) {
    const uint64_t token = job.token;
    auto& gate = gates[job.deck];
    if (!gate.isCurrent(token))
        return; // superseded before start

    auto publish = [this, &job, token](LoadResult&& r) {
        r.token = token;
        r.deck = job.deck;
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            results[job.deck] = std::move(r);
            resultReady[job.deck] = true;
        }
        triggerAsyncUpdate(); // commit runs on the message thread
    };

    lean::DecodedAudio decoded;
    std::string err;
    if (!lean::decodeFile(job.path, decoded, err)) {
        LoadResult r;
        r.ok = false;
        r.error = err;
        r.fileName = job.fileName;
        publish(std::move(r));
        return;
    }
    if (!gate.isCurrent(token))
        return; // superseded mid-decode: drop silently

    states[job.deck].store((int)lean::LoadState::Analyzing,
                           std::memory_order_seq_cst);
    const int frames = (int)(decoded.stereo.size() / 2);
    const double rate = decoded.sampleRate;
    LoadResult r;
    r.ok = true;
    r.fileName = job.fileName;
    r.bpm = lean::detectBpm(decoded.stereo.data(), frames, rate);
    r.wave = lean::buildWaveform(decoded.stereo.data(), (std::int64_t)frames,
                                 rate);
    r.stereo = std::move(decoded.stereo);
    r.rate = rate;
    if (!gate.isCurrent(token))
        return; // superseded during analysis: drop silently
    publish(std::move(r));
}

void AudioEngine::handleAsyncUpdate() {
    LoadResult local[2];
    bool ready[2] = { false, false };
    juce::StringArray newFxList;
    bool fxReady = false;
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        for (int d = 0; d < 2; ++d) {
            if (resultReady[d]) {
                local[d] = std::move(results[d]);
                resultReady[d] = false;
                ready[d] = true;
            }
        }
        if (fxResultReady) {
            newFxList = std::move(fxScanResult);
            fxResultReady = false;
            fxReady = true;
        }
    }
    if (fxReady)
        fxList = std::move(newFxList); // message thread owns the cache
    for (int d = 0; d < 2; ++d) {
        if (!ready[d])
            continue;
        LoadResult& r = local[d];
        if (!gates[d].accept(r.token))
            continue; // newest-wins: a newer submission owns this deck
        if (!r.ok) {
            // Previous track (if any) stays loaded; the attempt failed.
            errors[d] = juce::String(r.error);
            names[d] = {};
            states[d].store((int)lean::LoadState::Error,
                            std::memory_order_seq_cst);
            continue;
        }
        // Commit on the message thread: every reader (paint, timer) runs on
        // this thread, so each field below is observed atomically as a set.
        waves[d] = std::move(r.wave);
        bpm[d] = r.bpm;
        names[d] = juce::String(r.fileName);
        errors[d] = {};
        fileLoaded[d] = true;
        states[d].store((int)lean::LoadState::Ready, std::memory_order_seq_cst);
        mixer.channel(d).deck.load(std::move(r.stereo), r.rate); // staged
        if (auto* device = deviceManager.getCurrentAudioDevice())
            mixer.setOutputRate(device->getCurrentSampleRate());
        mixer.reset(); // deferred EQ clear (Eq3::reset is a request)
        if (onDeckReady)
            onDeckReady(d);
    }
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
    frameCount.store(blockFrame, std::memory_order_seq_cst);

    // Render watchdog: worst-case time and overruns past the block budget.
    const double ticksPerSec =
        (double)juce::Time::getHighResolutionTicksPerSecond();
    if (ticksPerSec > 0.0 && deviceRate > 0.0) {
        const long long us =
            (long long)((double)(t1 - t0) * 1000000.0 / ticksPerSec);
        long long prev = maxRenderUsUs.load(std::memory_order_seq_cst);
        while (us > prev
               && !maxRenderUsUs.compare_exchange_weak(prev, us,
                                                      std::memory_order_seq_cst)) {
        }
        const long long budget =
            (long long)((double)numSamples * 1000000.0 / deviceRate);
        if (us > budget)
            overruns.fetch_add(1, std::memory_order_seq_cst);
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
