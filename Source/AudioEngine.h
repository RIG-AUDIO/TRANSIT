// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Mixer.h"
#include "Waveform.h"
#include "Library.h"
#include "LoadSchedule.h"
#include "Recorder.h"
#include "Analyze.h"
#include "MidiMap.h"
#include "MidiQueue.h"
#include "Flx4Map.h"
#include "FxRack.h"

// JUCE shell around the JUCE-free lean core: owns the audio device, pulls
// the mix in the realtime callback, ships master peaks to the UI through a
// lock-free FIFO. File decoding, MIDI routing and recording glue live here;
// DSP lives in lean::*. File/recording I/O happens off the audio thread.
//
// Async file pipeline (review S5/S6): loadFile/indexFolder return at once;
// a single worker thread decodes, builds the waveform payload and detects
// BPM off the message thread. The worker never touches engine state — it
// publishes into a result slot and wakes the message thread via
// AsyncUpdater, which commits everything (waves/bpm/state/deck staging).
// Newest-wins per deck via lean::LoadGate: superseded jobs drop at every
// stage boundary and can never publish a stale result.
class AudioEngine : public juce::AudioIODeviceCallback,
                    private juce::MidiInputCallback,
                    private juce::AsyncUpdater {
public:
    AudioEngine();
    ~AudioEngine() override;

    // Async: queues the job and returns immediately. Progress through
    // deckState(); a completed load invokes onDeckReady on the message
    // thread; a failure lands in deckError() with state Error (the deck
    // keeps its previous track).
    void loadFile(int deck, const juce::String& path);
    int deckState(int deck) const {
        return states[deck & 1].load(std::memory_order_seq_cst);
    }
    juce::String deckName(int deck) const { return names[deck & 1]; }
    juce::String deckError(int deck) const { return errors[deck & 1]; }
    // Message thread, invoked after a load commits (auto-play etc.).
    std::function<void(int)> onDeckReady;

    bool hasFile(int deck) const { return fileLoaded[deck & 1]; }

    void setTempo(int deck, double t) { mixer.channel(deck & 1).deck.setTempo(t); }
    double getTempo(int deck) const {
        return mixer.channel(deck & 1).deck.getTempo();
    }

    // Beatmatching modes. Free behaves like a sync-less CDJ (no sync UI).
    // Sync adds per-deck tempo followers (tempo lock; phase stays manual).
    enum class BeatMode { Free, Sync };
    void setBeatMode(BeatMode m) {
        mode = m;
        if (m == BeatMode::Free)
            sync[0] = sync[1] = false;
    }
    BeatMode beatMode() const { return mode; }

    // Engage deck as tempo follower of the other deck. Returns false when
    // either BPM is unknown. Single follower: engaging one releases the other.
    bool setSync(int deck, bool on) {
        deck &= 1;
        if (mode != BeatMode::Sync || !on) {
            sync[deck] = false;
            return true;
        }
        if (bpm[deck] <= 0.0 || bpm[deck ^ 1] <= 0.0)
            return false;
        sync[deck] = true;
        sync[deck ^ 1] = false;
        return true;
    }
    bool isSync(int deck) const { return sync[deck & 1]; }

    // Message thread, called at UI rate: chase the leader's effective tempo.
    void updateSync() {
        for (int d = 0; d < 2; ++d) {
            if (!sync[d])
                continue;
            const double own = bpm[d];
            const double leadEff = bpm[d ^ 1] * getTempo(d ^ 1);
            const double t = lean::Deck::syncTempoFor(own, leadEff);
            if (t < 0.0) {
                sync[d] = false;
                continue;
            }
            mixer.channel(d).deck.setTempo(t);
        }
    }
    void setGain(int deck, float g) { mixer.channel(deck & 1).deck.setGain(g); }
    void setKeylock(int deck, bool on) { mixer.channel(deck & 1).deck.setKeylock(on); }
    void play(int deck, bool p) { mixer.channel(deck & 1).deck.play(p); }
    bool isPlaying(int deck) const { return mixer.channel(deck & 1).deck.isPlaying(); }
    double getPositionSeconds(int deck) const {
        return mixer.channel(deck & 1).deck.getPositionSeconds();
    }
    double getLengthSeconds(int deck) const {
        return mixer.channel(deck & 1).deck.getLengthSeconds();
    }

    void setCrossfader(float v) { mixer.setCrossfader(v); }
    void setMaster(float v) { mixer.setMaster(v); }
    void setPhonesLevel(float v) { mixer.setPhonesLevel(v); }
    void setCue(int deck, bool on) = delete; // use setPfl / cue preview below
    void setPfl(int deck, bool on) {
        pfl[deck & 1] = on;
        mixer.setCue(deck & 1, on || previewing[deck & 1]);
    }
    bool isPfl(int deck) const { return pfl[deck & 1]; }
    bool isPreviewing(int deck) const { return previewing[deck & 1]; }
    void setCueBlend(float v) { mixer.setCueBlend(v); }
    bool hasPhones() const { return fourOuts; }

    // Transport CUE (CDJ-style): playing + press -> pause at cue point;
    // paused + hold -> preview from cue point, release -> back to cue.
    void startCuePreview(int deck) {
        auto& d = mixer.channel(deck & 1).deck;
        if (d.isPlaying()) {
            d.play(false);
            d.setPositionSeconds(d.getCuePointSeconds());
        } else {
            d.setPositionSeconds(d.getCuePointSeconds());
            d.play(true);
            previewing[deck & 1] = true;
            mixer.setCue(deck & 1, true);
        }
    }
    void stopCuePreview(int deck) {
        deck &= 1;
        if (!previewing[deck])
            return;
        auto& d = mixer.channel(deck).deck;
        d.play(false);
        d.setPositionSeconds(d.getCuePointSeconds());
        previewing[deck] = false;
        mixer.setCue(deck, pfl[deck]);
    }

    // Jump by beats at the analyzed tempo (seconds fallback without BPM).
    void jumpBeats(int deck, double beats) {
        auto& d = mixer.channel(deck & 1).deck;
        const double b = bpm[deck & 1];
        const double sec = b > 0.0 ? beats * 60.0 / b : beats;
        d.setPositionSeconds(d.getPositionSeconds() + sec);
    }
    // Jog nudge: temporary rate offset, -1..1 (see Deck::setBend).
    void setBend(int deck, double b) { mixer.channel(deck & 1).deck.setBend(b); }
    double getBpm(int deck) const { return bpm[deck & 1]; }

    void seekFraction(int deck, double fraction) {
        deck &= 1;
        auto& d = mixer.channel(deck).deck;
        const double len = d.getLengthSeconds();
        if (len <= 0.0)
            return;
        d.setPositionSeconds(std::clamp(fraction, 0.0, 1.0) * len);
        if (!d.isPlaying())
            d.setCuePointSeconds(std::clamp(fraction, 0.0, 1.0) * len);
    }
    const lean::Waveform& getWaveform(int deck) const { return waves[deck & 1]; }

    // Folder index: async scan into the SQLite library (worker thread;
    // Library serializes against message-thread searches). Progress via
    // scanActive(), result count via scanCount() (-1 before any scan).
    void indexFolder(const juce::String& path);
    bool scanActive() const { return scanning.load(std::memory_order_seq_cst); }
    int scanCount() const { return lastScanCount.load(std::memory_order_seq_cst); }
    std::vector<std::string> searchLibrary(const std::string& query) {
        ensureDb();
        return library.search(query);
    }
    // Indexed metadata (tempo estimate, duration) for the given paths. The
    // browser uses it to fill its metadata columns without touching the disk.
    std::vector<std::pair<std::string, lean::Library::TrackMeta>> libraryMetadata(
        const std::vector<std::string>& pathsUtf8) {
        ensureDb();
        return library.metadataFor(pathsUtf8);
    }

    // MIDI input devices as (name, identifier-independent index). The UI maps
    // combo indices straight back into openMidiInput.
    juce::StringArray midiInputNames() const;
    bool openMidiInput(int index); // -1 closes

    // Realtime MIDI routing grid (message binds, audio looks up).
    void bindMidi(int channel, int cc, int deck, lean::MidiTarget target) {
        midiGrid.bind(channel, cc, deck, target);
    }
    void clearMidiGrid() { midiGrid.clear(); }
    long long midiDrops() const { return midiQueue.droppedCount(); }
    // Last-seen raw CC for MIDI-learn (any thread; atomics).
    uint64_t midiLearnStamp() const { return learnStamp.load(); }
    void midiLearnEvent(int& channel, int& cc) const {
        channel = learnCh.load();
        cc = learnCc.load();
    }

    // DDJ-FLX4 factory map: installs the value table (14-bit pairs) into
    // the grid and turns on note/encoder action parsing on the MIDI thread.
    // Actions are drained on the message thread (transport needs the UI).
    void setFlx4Mode(bool on);
    bool flx4Mode() const { return flx4Active.load(std::memory_order_seq_cst); }
    int drainFlx4Actions(lean::Flx4ActionMsg* out, int maxOut) {
        return flx4Queue.drain(out, maxOut);
    }
    long long flx4Drops() const { return flx4Queue.droppedCount(); }
    // LED echo + resync SysEx over the opened MIDI output (no-ops without).
    void sendFlx4Note(int channel, int note, bool on);
    void sendFlx4SysEx(const unsigned char* bytes, int numBytes);
    // 14-bit fold entry point for learn-free tables; returns combined
    // 0..16383 or -1 when `cc` is not half of a bound 14-bit pair.
    int midiIngest14(int channel, int cc, int value7) {
        return midiGrid.ingest14(channel, cc, value7);
    }
    void bindMidi14(int channel, int msb, int deck, lean::MidiTarget target) {
        midiGrid.bind14(channel, msb, deck, target);
    }

    // Render watchdog: worst block render time and overruns past the block
    // budget, for the Setup dialog. Updated on the audio thread.
    long long maxRenderUs() const { return maxRenderUsUs.load(); }
    long long renderOverruns() const { return overruns.load(); }
    void resetRenderStats() {
        maxRenderUsUs.store(0);
        overruns.store(0);
    }

    float getFader(int deck) const;
    float getMaster() const;
    float getPhonesLevel() const;
    float getXfade() const;
    float getBlend() const;
    float getGain(int deck) const;
    float getSend(int deck, int aux) const;
    void getEq(int deck, double& lo, double& mi, double& hi) const;

    // Mix recording to Music/TRANSIT/*.wav. Status text describes the result.
    bool toggleRecord(juce::String& status);
    bool isRecording() const { return recorder.isRecording(); }

    // Device setup for the settings dialog. Setters return an error string,
    // empty on success, and always keep the mixer rate in sync.
    juce::StringArray audioDeviceTypes();
    juce::String currentAudioDeviceType();
    void setAudioDeviceType(const juce::String& type);
    juce::StringArray outputDeviceNames();
    juce::String currentOutputDevice() const;
    juce::String setOutputDevice(const juce::String& name);
    juce::Array<int> availableSampleRates();
    juce::String setSampleRate(double rate);
    juce::Array<int> availableBufferSizes();
    juce::String setBufferSize(int samples);
    double cpuUsage() const { return deviceManager.getCpuUsage(); }
    int outputLatencySamples() const;
    double currentSampleRate() const;
    int currentBufferSize() const;
    void setKeylockEco(bool eco);
    bool keylockEco() const { return keylockEcoState; }
    bool clearLibrary();

    // Aux FX rack (VST3). Message thread for load/unload/prepare/editors.
    bool loadAuxPlugin(int aux, const juce::String& bundlePath, juce::String& error) {
        return fxRack.loadPlugin(aux, bundlePath, error);
    }
    void unloadAuxPlugin(int aux) { fxRack.unloadPlugin(aux); }
    void setAuxBypass(int aux, bool bypassed) { fxRack.setBypass(aux, bypassed); }
    bool auxBypassed(int aux) const { return fxRack.isBypassed(aux); }
    juce::String auxName(int aux) const { return fxRack.slotName(aux); }
    juce::String auxPath(int aux) const { return fxRack.slotPath(aux); }
    bool auxHasEditor(int aux) const { return fxRack.hasEditor(aux); }
    void showAuxEditor(int aux) { fxRack.showEditor(aux); }
    // Async VST3 discovery (review S9): the directory walk runs on the
    // worker; fxPlugins() returns the message-thread cache (empty until the
    // first scan commits). refreshFxScan() is single-flight — at most one
    // walk queued at a time.
    void refreshFxScan();
    juce::StringArray fxPlugins() const { return fxList; } // message thread
    bool fxScanRunning() const {
        return fxScanActive.load(std::memory_order_seq_cst);
    }
    void pumpFx() { fxRack.pumpRetired(); }
    void prepareFx(double rate) {
        // Prepare plugins for the REAL device block size: a 1024 assumption
        // breaks the hosting contract on devices with larger callbacks.
        int block = 1024;
        if (auto* device = deviceManager.getCurrentAudioDevice())
            block = juce::jmax(16, device->getCurrentBufferSizeSamples());
        fxRack.prepare(rate, block);
    }
    bool fxRateDirty() const { return fxRack.isRateDirty(); }
    void setSend(int deck, int aux, float v);
    void setAuxReturn(int aux, float v);
    float getAuxReturn(int aux) const;
    void setEq(int deck, double lowDb, double midDb, double highDb) {
        mixer.setEq(deck & 1, lowDb, midDb, highDb);
    }
    void setFader(int deck, float v) { mixer.setFader(deck & 1, v); }

    float takePeak();

    struct PeakFrame {
        float master = 0.0f;
        float chA = 0.0f;
        float chB = 0.0f;
    };
    // Drains block peaks since the last call; each out is the max observed.
    void takePeaks(float& master, float& chA, float& chB);

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                         int numInputChannels,
                                         float* const* outputChannelData,
                                         int numOutputChannels,
                                         int numSamples,
                                         const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override { fxRack.setAudioRunning(false); }

private:
    void ensureDb() {
        if (dbReady)
            return;
        auto dir = juce::File::getSpecialLocation(
                       juce::File::userApplicationDataDirectory)
                       .getChildFile("TRANSIT");
        dir.createDirectory();
        std::string error;
        dbReady = library.open(
            dir.getChildFile("library.db").getFullPathName().toStdString(), error);
    }

    // Worker-thread jobs (guarded by jobMutex) and finished results
    // (guarded by resultMutex). Nothing in here is ever touched by the
    // audio thread.
    struct LoadJob {
        enum class Kind { Load, Index, ScanFx };
        Kind kind = Kind::Load;
        int deck = 0;
        uint64_t token = 0;
        std::string path;
        std::string fileName;
    };
    struct LoadResult {
        uint64_t token = 0;
        int deck = 0;
        bool ok = false;
        std::string error;
        std::string fileName;
        std::vector<float> stereo;
        double rate = 44100.0;
        double bpm = 0.0;
        lean::Waveform wave;
    };

    void workerLoop();
    void runLoadJob(const LoadJob& job);
    void runIndexJob(const LoadJob& job);
    void runScanFxJob();
    void handleAsyncUpdate() override;

    juce::AudioDeviceManager deviceManager;
    lean::Mixer mixer;
    FxRack fxRack;
    // Committed exclusively on the message thread (handleAsyncUpdate): paint
    // and the UI timer run on the same thread, so a commit is observed as a
    // whole old-or-new value — no lock, no torn payload. The worker only
    // ever fills its local LoadResult.
    lean::Waveform waves[2];
    lean::Library library;
    lean::Recorder recorder;
    lean::LoadGate gates[2];
    std::atomic<int> states[2]{}; // lean::LoadState, worker writes Analyzing
    juce::String names[2];  // message thread only (committed filenames)
    juce::String errors[2]; // message thread only
    std::mutex jobMutex;
    std::condition_variable jobCv;
    std::deque<LoadJob> jobs;
    bool quitting = false; // guarded by jobMutex
    std::mutex resultMutex;
    LoadResult results[2];
    bool resultReady[2] = { false, false }; // guarded by resultMutex
    std::thread worker;
    std::atomic<bool> scanning{ false };
    std::atomic<int> lastScanCount{ -1 };
    std::atomic<bool> fxScanActive{ false }; // single-flight ScanFx gate
    juce::StringArray fxList;      // message thread only (committed cache)
    juce::StringArray fxScanResult; // worker write; guarded by resultMutex
    bool fxResultReady = false;     // guarded by resultMutex
    bool dbReady = false;
    bool fileLoaded[2] = { false, false };
    bool fourOuts = false;
    bool keylockEcoState = false;
    bool pfl[2] = { false, false };
    bool previewing[2] = { false, false };
    bool sync[2] = { false, false };
    BeatMode mode = BeatMode::Free;
    double bpm[2] = { 0.0, 0.0 };
    std::unique_ptr<juce::MidiInput> midiIn;
    std::unique_ptr<juce::MidiOutput> midiOut; // LED echo / SysEx resync
    lean::MidiGrid midiGrid;
    lean::MidiQueue midiQueue;
    std::atomic<bool> flx4Active{ false };
    lean::Flx4ActionQueue flx4Queue; // MIDI thread -> message thread
    lean::Pickup pickups;            // audio thread only (applyMidiEvent)
    std::atomic<uint64_t> frameCount{ 0 };
    std::atomic<int> learnCh{ -1 };
    std::atomic<int> learnCc{ -1 };
    std::atomic<uint64_t> learnStamp{ 0 };
    uint64_t blockFrame = 0; // audio thread only
    double deviceRate = 44100.0; // set in AboutToStart, audio thread only
    std::atomic<long long> maxRenderUsUs{ 0 };
    std::atomic<long long> overruns{ 0 };

    void handleIncomingMidiMessage(juce::MidiInput*,
                                   const juce::MidiMessage& message) override;
    juce::String applySetup(juce::AudioDeviceManager::AudioDeviceSetup setup);
    void applyMidiEvent(const lean::MidiEvent& ev);
    // Current app value of a MIDI target in 0..1 (soft-takeover reference).
    float currentTarget01(lean::MidiTarget target, int deck) const;

    static constexpr int meterSize = 64;
    PeakFrame peakSlots[meterSize] = {};
    juce::AbstractFifo peakFifo{ meterSize };
};
