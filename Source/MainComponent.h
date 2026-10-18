// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <optional>
#include <vector>

#include "AudioEngine.h"
#include "DJLook.h"
#include "MidiMap.h"
#include "TopBar.h"

// M3 UI: two deck strips (controls + waveform with click-seek), mixer row,
// folder browser (double-click loads to the selected deck), master meter.
class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void dumpLayout() const;

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

    private:
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

    // Thin vertical channel VU meter, Xone-style: green to amber to red.
    class ChannelMeter : public juce::Component {
    public:
        void setLevel(float v) {
            level = v;
            repaint();
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xff161a20));
            const float h = juce::jlimit(0.0f, 1.0f, level) * (float)getHeight();
            if (h <= 0.0f)
                return;
            juce::ColourGradient grad(DJLook::cyan, 0.0f, (float)getHeight(),
                                      DJLook::alarm, 0.0f, 0.0f, false);
            grad.addColour(0.6, DJLook::accent);
            g.setGradientFill(grad);
            g.fillRect(0, getHeight() - (int)h, getWidth(), (int)h);
        }

    private:
        float level = 0.0f;
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
        juce::Label status;
        juce::Label time;
        std::unique_ptr<WaveformView> wave;
    };

    struct FileList : public juce::ListBoxModel {
        juce::StringArray files;
        std::function<void(int)> onDoubleClick;
        int getNumRows() override { return files.size(); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                              bool selected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
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
    float chanShown[2] = { 0.0f, 0.0f };

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
    float meterShown = 0.0f;
};
