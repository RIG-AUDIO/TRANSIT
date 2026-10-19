// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <optional>
#include <vector>

#include "AudioEngine.h"
#include "DJLook.h"
#include "MidiMap.h"
#include "TopBar.h"
#include "UiState.h"

// M3 UI: two deck strips (controls + waveform with click-seek), mixer row,
// folder browser (double-click loads to the selected deck), master meter.
class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void dumpLayout() const;

    // CI smoke-test seams (docs/development/CI_VISUAL_TESTING.md): thin
    // wrappers so Tools/GuiSmoke.cpp can drive deterministic UI states
    // without reaching into private members. No behavior lives here.
    void harnessLoadFile(int deck, const juce::String& path) {
        engine.loadFile(deck, path);
    }
    void harnessPlay(int deck, bool p) { engine.play(deck, p); }
    int harnessDeckState(int deck) const { return engine.deckState(deck); }
    void harnessSetZoom(int deck, int idx) {
        strips[deck & 1].wave->setZoomIndex(idx);
    }
    void harnessShowFxPage(bool show) { showBrowser(!show); }
    // G4: seed the meters to known dB-mapped levels so CI snapshots can see
    // the fill and peak-hold marker without an audio device.
    void harnessSetMeters(float master, float chA, float chB) {
        meterShown = master;
        meterHold.push(master);
        chanShown[0] = chA;
        chanShown[1] = chB;
        chanMeters[0].setLevel(chA);
        chanMeters[1].setLevel(chB);
        // Top-bar master history ring: one full ramp sweep (96 = MasterStrip
        // depth) of raw linear peaks, incl. a clipping tail.
        for (int i = 0; i < 96; ++i)
            topBar.pushMaster(0.30f + 0.0074f * (float)i);
        repaint(getLocalBounds().removeFromBottom(26));
    }
    // G6: seed the browser folder so CI frames show real metadata rows.
    void harnessSetBrowserPath(const juce::String& dir) {
        browserDir = juce::File(dir);
        indexedFolderLabel = dir;
        folderLabel.setText(dir, juce::dontSendNotification);
        refreshBrowserList();
        fileList.selectRow(0); // surface the G2 selection pill in shots
    }

private:
    class WaveformView : public juce::Component {
    public:
        WaveformView(AudioEngine& e, int deck);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseWheelMove(const juce::MouseEvent& event,
                            const juce::MouseWheelDetails& wheel) override;
        void zoomIn();
        void zoomOut();
        void setZoomIndex(int idx) {
            const int c = lean::clampZoomIndex(idx);
            if (c != zoomIdx) {
                zoomIdx = c;
                repaint();
                notifyZoomChanged();
            }
        }
        int getZoomIndex() const { return zoomIdx; }
        // W8: fired whenever the zoom index changes so the owner can persist
        // it (the settings file is the only cross-session state we keep).
        std::function<void()> onZoomChanged;

    private:
        void notifyZoomChanged() {
            if (onZoomChanged)
                onZoomChanged();
        }
        void paintOverview(juce::Graphics& g, int W, int mainH, int stripeH,
                           double len, double pos);
        static constexpr int kStripeHeight = 14; // whole-track overview bar
        // Window this view last rendered; mouse seek maps through it (W3).
        // windowSpan == 0 until the first paint -> whole-track fallback.
        double windowStart = 0.0;
        double windowSpan = 0.0;
        int zoomIdx = 3; // 32 s, Traktor-like default scope (lean::kZoomSeconds)
        AudioEngine& engine;
        int deck;
    };

    // Shared meter peak-hold (G4): instant attack, ~0.75 s hold at the 30 Hz
    // meter tick, then a slow constant fall (~14 dB/s over the -60..0 span).
    struct MeterHold {
        float value = 0.0f;
        int age = 0;
        void push(float unit) {
            if (unit >= value) {
                value = unit;
                age = 22;
            } else if (age > 0) {
                --age;
            } else {
                value = juce::jmax(unit, value - 0.008f);
            }
        }
    };

    // Thin vertical channel VU meter, Xone-style: dB-scaled (-60..0, review
    // G4) cyan -> amber -> red with an ink peak-hold hairline. `unit` comes
    // from DJLook::meterUnit.
    class ChannelMeter : public juce::Component {
    public:
        void setLevel(float unit) {
            level = unit;
            hold.push(unit);
            repaint();
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(DJLook::well);
            const float h = juce::jlimit(0.0f, 1.0f, level) * (float)getHeight();
            if (h > 0.0f) {
                juce::ColourGradient grad(DJLook::cyan, 0.0f, (float)getHeight(),
                                          DJLook::alarm, 0.0f, 0.0f, false);
                grad.addColour(0.6, DJLook::accent);
                g.setGradientFill(grad);
                g.fillRect(0, getHeight() - (int)h, getWidth(), (int)h);
            }
            if (hold.value > 0.004f) {
                const int y = juce::jmax(
                    0, getHeight() - (int)(hold.value * (float)getHeight()) - 2);
                g.setColour(DJLook::ink);
                g.fillRect(0, y, getWidth(), 2);
            }
        }

    private:
        float level = 0.0f;
        MeterHold hold;
    };

    struct CueHold : public juce::MouseListener {
        MainComponent* owner = nullptr;
        int deck = 0;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
    };

    struct DeckStrip {
        juce::TextButton open{ "Open" };
        juce::TextButton play{ "Play" };
        juce::TextButton cue{ "CUE" };
        juce::TextButton pfl{ "PFL" };
        juce::TextButton key{ "Key" };
        juce::TextButton sync{ "SYNC" };
        juce::TextButton back{ "<" };
        juce::TextButton fwd{ ">" };
        juce::Slider tempo{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider gain{ juce::Slider::RotaryHorizontalVerticalDrag,
                           juce::Slider::TextBoxBelow };
        juce::Slider low{ juce::Slider::RotaryHorizontalVerticalDrag,
                          juce::Slider::TextBoxBelow };
        juce::Slider mid{ juce::Slider::RotaryHorizontalVerticalDrag,
                          juce::Slider::TextBoxBelow };
        juce::Slider high{ juce::Slider::RotaryHorizontalVerticalDrag,
                           juce::Slider::TextBoxBelow };
        juce::Slider fader{ juce::Slider::LinearVertical, juce::Slider::NoTextBox };
        juce::Label header;
        juce::TextButton zoomInBtn{ "+" };
        juce::TextButton zoomOutBtn{ "-" };
        juce::Label knobCaps[4];
        juce::Label stateChip; // G5: dedicated state word (EMPTY/…/ERROR)
        juce::Label status;    // G5 payload: filename or error text
        juce::Label time;
        std::unique_ptr<WaveformView> wave;
    };

    struct FileList : public juce::ListBoxModel {
        // G6: full path plus precomputed display columns — paint never
        // touches the disk.
        struct Row {
            juce::String path;
            juce::String name;
            juce::String folder;
            juce::String size;
            juce::String date;
            juce::String bpm;  // G6 round 2: tempo estimate ("—" when unknown)
            juce::String time; // G6 round 2: duration, m:ss
        };
        std::vector<Row> rows;
        std::function<void(int)> onDoubleClick;
        int getNumRows() override { return (int)rows.size(); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                              bool selected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
        void returnKeyPressed(int row) override; // G6: Enter loads selection
    };

    void timerCallback() override;
    void openFile(int deck);
    void openFolder();
    void openSettings();
    void setBeatModeUI(bool syncMode);
    void showBrowser(bool show);
    void pickAuxPlugin(int aux);
    juce::File settingsFile() const;
    void loadSettings();
    void saveSettings();
    void refreshBrowserList();
    void loadBrowserRow(int row);
    // G6 round 2: header-only duration probe for files that are not in the
    // library index (no full decode, no index write).
    juce::String probeDuration(const juce::File& file) const;
    void layoutDeck(DeckStrip& s, juce::Rectangle<int> panel);
    void applyEq(int deck);
    void updateDeckHeader(int deck);
    void loadMidiMap();
    void saveMidiMap();
    juce::Slider* sliderFor(int deck, lean::MidiTarget target);
    juce::File midiMapFile() const;
    juce::String formatTime(double seconds) const;

    AudioEngine engine;
    DeckStrip strips[2];
    // G6 round 2: header-only duration probe for files outside the index.
    juce::AudioFormatManager audioFormats;
    juce::Slider xfade{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider cueBlend{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton folderButton{ "Folder..." };
    juce::TextButton toA{ "To A" };
    juce::TextButton toB{ "To B" };
    juce::Label folderLabel;
    juce::Label libraryHeader;
    juce::TextEditor searchBox;
    GlassPanel browserGlass;
    // Declaration order matters: fileList borrows &fileModel in its
    // constructor, so the model must be declared (and built) first.
    FileList fileModel;
    juce::ListBox fileList;
    juce::File browserDir;
    juce::ComboBox midiCombo;
    juce::TextButton learnButton{ "Learn" };
    juce::Label sysStatus;
    TopBar topBar;
    lean::MidiMap midiMap;
    struct PendingLearn {
        int deck;
        lean::MidiTarget target;
    };
    std::optional<PendingLearn> pending;
    juce::Label mixerCaps[3];
    juce::Label mixerHeader;
    juce::Label chanCaps[2];
    ChannelMeter chanMeters[2];
    float chanShown[2] = { 0.0f, 0.0f }; // G4: dB-mapped unit (0..1)

    struct AuxStrip {
        juce::Label header;
        juce::TextButton slot{ "Empty" };
        juce::TextButton bypass{ "ON" };
        juce::TextButton edit{ "Edit" };
        juce::Slider sendA{ juce::Slider::RotaryHorizontalVerticalDrag,
                            juce::Slider::NoTextBox };
        juce::Slider sendB{ juce::Slider::RotaryHorizontalVerticalDrag,
                            juce::Slider::NoTextBox };
        juce::Slider ret{ juce::Slider::RotaryHorizontalVerticalDrag,
                          juce::Slider::NoTextBox };
        juce::Label capA{ {}, "A" };
        juce::Label capB{ {}, "B" };
        juce::Label capR{ {}, "RET" };
    };
    AuxStrip auxStrips[4];
    juce::Component fxPage;
    uint64_t learnSeen = 0;
    bool showBpm = true;
    CueHold cueHolds[2];
    juce::String lastHeader[2];
    // Debounced library search (review S6): one-shot timer restarted on
    // every keystroke; refreshBrowserList runs when typing pauses.
    struct SearchDebounce : juce::Timer {
        MainComponent* owner = nullptr;
        void timerCallback() override;
    };
    SearchDebounce searchDebounce;
    juce::String indexedFolderLabel; // folder chosen for the async scan
    bool scanNoteActive = false;     // a scan is running (label "scanning")
    DJLook look;
    float meterShown = 0.0f; // G4: dB-mapped unit (0..1)
    MeterHold meterHold;
    int statusAnimTick = 0; // G5: drives the LOADING... dot animation
    // W8: set while the stored zoom index is restored at startup, so the
    // restore itself does not write the settings file back.
    bool suppressZoomSave = false;
    // G8 finish: per-deck change tokens for waveform invalidation.
    int waveStateSeen[2] = { -1, -1 };
    int64_t wavePosSeen[2] = { 0, 0 };
};
