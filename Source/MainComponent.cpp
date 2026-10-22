// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "MainComponent.h"

#include <cstdio>
#include <map>

#include "SettingsDialog.h"

namespace {
// G4: constant meter-bar fall in unit space (~18 dB/s at the 30 Hz tick).
constexpr float kMeterFall = 0.01f;
} // namespace

MainComponent::WaveformView::WaveformView(AudioEngine& e, int d)
    : engine(e), deck(d) {}

void MainComponent::WaveformView::zoomIn() {
    const int idx = lean::clampZoomIndex(zoomIdx - 1);
    if (idx != zoomIdx) {
        zoomIdx = idx;
        repaint(); // W4: zoom changes repaint explicitly, not via the timer
        notifyZoomChanged(); // W8: persist the zoom index across sessions
    }
}

void MainComponent::WaveformView::zoomOut() {
    const int idx = lean::clampZoomIndex(zoomIdx + 1);
    if (idx != zoomIdx) {
        zoomIdx = idx;
        repaint(); // W4
        notifyZoomChanged(); // W8
    }
}

void MainComponent::WaveformView::mouseWheelMove(const juce::MouseEvent&,
                                                 const juce::MouseWheelDetails& wheel) {
    if (wheel.deltaY > 0.0f)
        zoomIn();
    else if (wheel.deltaY < 0.0f)
        zoomOut();
}

void MainComponent::WaveformView::paint(juce::Graphics& g) {
    g.fillAll(DJLook::deep);
    const int W = getWidth();
    const int H = getHeight();
    const int stripe = (H >= 48) ? kStripeHeight : 0;
    const int mainH = H - stripe;
    const auto& wave = engine.getWaveform(deck);
    const double len = engine.getLengthSeconds(deck);
    if (wave.valid() && len > 0.0 && W > 0 && mainH > 0) {
        // Scrolling window centered on the playhead (clamped to the track).
        const double pos = engine.getPositionSeconds(deck);
        const double span = lean::zoomSpanSeconds(zoomIdx, len);
        double start = pos - span * 0.35; // playhead ~1/3 in, Traktor-style
        start = juce::jlimit(0.0, std::max(0.0, len - span), start);
        windowStart = start; // W3: record the exact window this frame draws
        windowSpan = span;   // so click-seek uses the identical formula
        for (int x = 0; x < W; ++x) {
            const double t0 = start + span * (double)x / W;
            const double t1 = start + span * (double)(x + 1) / W;
            float mn = 0.0f, mx = 0.0f;
            wave.aggregate(t0, t1, mn, mx); // W2: LOD min/max per pixel
            mn = juce::jlimit(-1.0f, 1.0f, mn);
            mx = juce::jlimit(-1.0f, 1.0f, mx);
            const int mid = mainH / 2;
            const int half = juce::jmax(1, mainH / 2 - 2);
            const int yTop = mid - (int)((double)mx * (double)half);
            const int yBot = mid - (int)((double)mn * (double)half);
            const bool played = t0 < pos;
            g.setColour(played ? DJLook::cyan.withAlpha(0.35f) : DJLook::cyan);
            g.fillRect(x, juce::jmin(yTop, yBot), 1, juce::jmax(1, yBot - yTop));
        }
        const double posX = (pos - start) / span * (double)W;
        g.setColour(DJLook::accent);
        g.fillRect(juce::jlimit(0, W - 2, (int)posX), 0, 2, mainH);
    } else {
        g.setColour(DJLook::dimInk);
        juce::String text;
        switch (static_cast<lean::LoadState>(engine.deckState(deck))) {
            case lean::LoadState::Empty:
                text = "Drop a track - double-click the library";
                break;
            case lean::LoadState::Loading: text = "Loading..."; break;
            case lean::LoadState::Analyzing: text = "Analyzing..."; break;
            case lean::LoadState::Ready:
                text = "Drop a track - double-click the library";
                break;
            case lean::LoadState::Error: text = "Load failed"; break;
        }
        const int msgH = mainH > 0 ? mainH : H;
        g.drawText(text, juce::Rectangle<int>(0, 0, W, msgH).reduced(8),
                   juce::Justification::centred);
    }
    if (stripe > 0)
        paintOverview(g, W, mainH, stripe, len,
                      engine.getPositionSeconds(deck));
}

// W5: whole-track overview stripe rendered from the pyramid, with its own
// playhead marker and the S5 load state while no waveform is committed yet.
void MainComponent::WaveformView::paintOverview(juce::Graphics& g, int W,
                                                int mainH, int stripeH,
                                                double len, double pos) {
    const juce::Rectangle<int> bar(0, mainH, W, stripeH);
    g.setColour(DJLook::well);
    g.fillRect(bar);
    const auto& wave = engine.getWaveform(deck);
    if (wave.valid() && len > 0.0 && W > 0) {
        for (int x = 0; x < W; ++x) {
            const double t0 = len * (double)x / W;
            const double t1 = len * (double)(x + 1) / W;
            float mn = 0.0f, mx = 0.0f;
            wave.aggregate(t0, t1, mn, mx);
            mn = juce::jlimit(-1.0f, 1.0f, mn);
            mx = juce::jlimit(-1.0f, 1.0f, mx);
            const int mid = mainH + stripeH / 2;
            const int half = juce::jmax(1, stripeH / 2 - 1);
            const int yTop = mid - (int)((double)mx * (double)half);
            const int yBot = mid - (int)((double)mn * (double)half);
            const bool played = t1 <= pos;
            g.setColour(played ? DJLook::cyan.withAlpha(0.45f)
                               : DJLook::cyan.withAlpha(0.75f));
            g.fillRect(x, juce::jmin(yTop, yBot), 1, juce::jmax(1, yBot - yTop));
        }
        const int px = juce::jlimit(0, W - 1, (int)(pos / len * (double)W));
        g.setColour(DJLook::accent);
        g.fillRect(px, mainH, 2, stripeH);
    } else {
        juce::String text;
        switch (static_cast<lean::LoadState>(engine.deckState(deck))) {
            case lean::LoadState::Empty: break;
            case lean::LoadState::Loading: text = "Loading..."; break;
            case lean::LoadState::Analyzing: text = "Analyzing..."; break;
            case lean::LoadState::Ready: break;
            case lean::LoadState::Error: break;
        }
        if (text.isNotEmpty()) {
            g.setColour(DJLook::dimInk);
            g.drawText(text, bar.reduced(4, 0), juce::Justification::centred);
        }
    }
}

void MainComponent::WaveformView::mouseDown(const juce::MouseEvent& event) {
    const int W = getWidth();
    const int H = getHeight();
    const int stripe = (H >= 48) ? kStripeHeight : 0;
    if (W <= 0)
        return;
    if (stripe > 0 && event.y >= H - stripe) {
        // W5: overview stripe seeks across the whole track, zoom-independent.
        engine.seekFraction(
            deck, juce::jlimit(0.0, 1.0, (double)event.x / (double)W));
        return;
    }
    const double len = engine.getLengthSeconds(deck);
    if (len <= 0.0)
        return;
    // W3: map through the exact window paint last rendered; before the first
    // paint fall back to the whole track.
    const double start = windowSpan > 0.0 ? windowStart : 0.0;
    const double span = windowSpan > 0.0 ? windowSpan : len;
    const double t = juce::jlimit(
        0.0, len, lean::seekTimeFromX(start, span, (double)event.x, (double)W));
    engine.seekFraction(deck, t / len);
}

void MainComponent::CueHold::mouseDown(const juce::MouseEvent&) {
    if (owner)
        owner->engine.startCuePreview(deck);
}

void MainComponent::CueHold::mouseUp(const juce::MouseEvent&) {
    if (owner)
        owner->engine.stopCuePreview(deck);
}

void MainComponent::FileList::paintListBoxItem(int row, juce::Graphics& g, int w,
                                              int h, bool selected) {
    // G6 columns: name | bpm | time | folder | size | date, all precomputed
    // in the model; the metadata columns drop out on narrow lists instead of
    // clipping. Selected rows keep the flat info-cyan pill (G2).
    if (selected) {
        auto area = juce::Rectangle<float>((float)w - 8.0f, (float)h - 4.0f)
                        .withPosition(4.0f, 2.0f);
        g.setColour(DJLook::cyan);
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(DJLook::bg);
    } else {
        g.setColour(DJLook::ink);
    }
    if (row < 0 || row >= (int)rows.size())
        return;
    const Row& r = rows[(size_t)row];
    const int dateW = w >= 480 ? 92 : 0;
    const int sizeW = w >= 480 ? 78 : 0;
    const int folderW = w >= 320 ? 96 : 0;
    const int timeW = w >= 640 ? 56 : 0;
    const int bpmW = w >= 760 ? 64 : 0;
    int x = 12;
    const int nameW = w - 20 - dateW - sizeW - folderW - timeW - bpmW;
    if (nameW > 40) {
        g.drawText(r.name, x, 0, nameW, h, juce::Justification::centredLeft,
                   true);
        x += nameW;
    }
    const auto metaColour = selected ? DJLook::bg : DJLook::dimInk;
    if (bpmW > 0) {
        g.setColour(metaColour);
        g.drawText(r.bpm, x, 0, bpmW - 8, h,
                   juce::Justification::centredRight, true);
        x += bpmW;
    }
    if (timeW > 0) {
        g.setColour(metaColour);
        g.drawText(r.time, x, 0, timeW - 8, h,
                   juce::Justification::centredRight, true);
        x += timeW;
    }
    if (folderW > 0) {
        g.setColour(metaColour);
        g.drawText(r.folder, x, 0, folderW, h, juce::Justification::centredLeft,
                   true);
        x += folderW;
    }
    if (sizeW > 0) {
        g.setColour(metaColour);
        g.drawText(r.size, x, 0, sizeW - 8, h,
                   juce::Justification::centredRight, true);
        x += sizeW;
    }
    if (dateW > 0) {
        g.setColour(metaColour);
        g.drawText(r.date, x, 0, dateW - 12, h,
                   juce::Justification::centredRight, true);
    }
}

void MainComponent::FileList::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    if (onDoubleClick)
        onDoubleClick(row);
}

void MainComponent::FileList::returnKeyPressed(int row) {
    if (onDoubleClick)
        onDoubleClick(row); // G6: Enter activates the selection too
}

MainComponent::MainComponent() : fileList("browser", &fileModel) {
    setLookAndFeel(&look);
    setWantsKeyboardFocus(true); // transport F-keys start here, then bubble
    audioFormats.registerBasicFormats(); // G6 round 2: duration probes
    const char* names[2] = { "A", "B" };
    const char* knobNames[4] = { "GAIN", "LOW", "MID", "HIGH" };
    for (int d = 0; d < 2; ++d) {
        auto& s = strips[d];
        addAndMakeVisible(s.header);
        addAndMakeVisible(s.zoomInBtn);
        addAndMakeVisible(s.zoomOutBtn);
        addAndMakeVisible(s.sync);
        s.sync.setVisible(false); // Free mode default: no sync buttons
        s.header.setText("DECK " + juce::String(names[d]), juce::dontSendNotification);
        s.header.setFont(juce::Font(juce::FontOptions{}.withHeight(17.0f)).boldened());
        s.header.setColour(juce::Label::textColourId, DJLook::accent);
        addAndMakeVisible(s.open);
        addAndMakeVisible(s.play);
        addAndMakeVisible(s.cue);
        addAndMakeVisible(s.pfl);
        addAndMakeVisible(s.key);
        addAndMakeVisible(s.back);
        addAndMakeVisible(s.fwd);
        addAndMakeVisible(s.tempo);
        addAndMakeVisible(s.gain);
        addAndMakeVisible(s.low);
        addAndMakeVisible(s.mid);
        addAndMakeVisible(s.high);
        addAndMakeVisible(s.fader);
        addAndMakeVisible(s.stateChip);
        addAndMakeVisible(s.status);
        addAndMakeVisible(s.time);
        for (int k = 0; k < 4; ++k) {
            addAndMakeVisible(s.knobCaps[k]);
            s.knobCaps[k].setText(knobNames[k], juce::dontSendNotification);
            s.knobCaps[k].setJustificationType(juce::Justification::centred);
            s.knobCaps[k].setColour(juce::Label::textColourId, DJLook::dimInk);
            s.knobCaps[k].setFont(
                juce::Font(juce::FontOptions{}.withHeight(11.0f)).boldened());
            // Deck B captions stay hidden: the mixer matrix reuses deck A's
            // labels as its row headers.
            s.knobCaps[k].setVisible(d == 0);
        }
        s.wave = std::make_unique<WaveformView>(engine, d);
        // W8/W9: persist the zoom index whenever it changes (button, wheel
        // or programmatic set) and remember it under the deck's file.
        s.wave->onZoomChanged = [this, d] {
            if (suppressZoomSave)
                return;
            rememberFileZoom(d);
            saveSettings();
        };
        addAndMakeVisible(s.wave.get());
        s.zoomInBtn.onClick = [this, d] {
            if (strips[d].wave)
                strips[d].wave->zoomIn();
        };
        s.zoomOutBtn.onClick = [this, d] {
            if (strips[d].wave)
                strips[d].wave->zoomOut();
        };

        s.open.setButtonText("Open " + juce::String(names[d]));
        s.open.onClick = [this, d] { openFile(d); };
        s.play.setClickingTogglesState(true);
        s.play.onClick = [this, d] {
            engine.play(d, strips[d].play.getToggleState());
        };
        s.sync.setClickingTogglesState(true);
        s.sync.onClick = [this, d] {
            setSyncFromUi(d, strips[d].sync.getToggleState());
        };
        s.key.setClickingTogglesState(true);
        s.key.onClick = [this, d] { engine.setKeylock(d, strips[d].key.getToggleState()); };
        s.pfl.setClickingTogglesState(true);
        s.pfl.onClick = [this, d] { engine.setPfl(d, strips[d].pfl.getToggleState()); };
        cueHolds[d].owner = this;
        cueHolds[d].deck = d;
        s.cue.addMouseListener(&cueHolds[d], false);
        s.back.onClick = [this, d] { engine.jumpBeats(d, -4.0); };
        s.fwd.onClick = [this, d] { engine.jumpBeats(d, 4.0); };

        s.tempo.setRange(0.5, 1.5, 0.001);
        s.tempo.setValue(1.0);
        s.tempo.onValueChange = [this, d] { engine.setTempo(d, strips[d].tempo.getValue()); };

        s.gain.setRange(0.0, 1.25, 0.001);
        s.gain.setValue(1.0);
        s.gain.onValueChange = [this, d] {
            engine.setGain(d, (float)strips[d].gain.getValue());
        };

        s.gain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        for (auto* eq : { &s.low, &s.mid, &s.high }) {
            eq->setRange(-24.0, 6.0, 0.1);
            eq->setValue(0.0);
            eq->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            eq->getProperties().set("bipolar", 1);
            eq->setDoubleClickReturnValue(true, 0.0);
            eq->onValueChange = [this, d] { applyEq(d); };
        }

        s.fader.setRange(0.0, 1.0, 0.001);
        s.fader.setValue(1.0);
        s.fader.onValueChange = [this, d] {
            engine.setFader(d, (float)strips[d].fader.getValue());
        };
        auto arm = [this, d](juce::Slider& sl, lean::MidiTarget t) {
            sl.onDragStart = [this, d, t] {
                if (learnButton.getToggleState()) {
                    pending = PendingLearn{ d, t };
                    sysStatus.setText("Move a MIDI control to bind...",
                                      juce::dontSendNotification);
                }
            };
        };
        arm(s.tempo, lean::MidiTarget::Tempo);
        arm(s.gain, lean::MidiTarget::Gain);
        arm(s.fader, lean::MidiTarget::Fader);
        arm(s.low, lean::MidiTarget::Low);
        arm(s.mid, lean::MidiTarget::Mid);
        arm(s.high, lean::MidiTarget::High);

        s.stateChip.setText("EMPTY", juce::dontSendNotification);
        s.stateChip.setColour(juce::Label::textColourId, DJLook::dimInk);
        s.stateChip.setFont(
            juce::Font(juce::FontOptions{}.withHeight(11.0f)).boldened());
        s.stateChip.setJustificationType(juce::Justification::centredLeft);
        s.status.setText({}, juce::dontSendNotification);
        s.time.setText("--:-- / --:--", juce::dontSendNotification);
    }

    addAndMakeVisible(xfade);
    addAndMakeVisible(cueBlend);
    xfade.setRange(0.0, 1.0, 0.001);
    xfade.setValue(0.5);
    xfade.onValueChange = [this] { engine.setCrossfader((float)xfade.getValue()); };
    cueBlend.setRange(0.0, 1.0, 0.001);
    cueBlend.setValue(1.0);
    cueBlend.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    cueBlend.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    cueBlend.onValueChange = [this] {
        engine.setCueBlend((float)cueBlend.getValue());
    };
    topBar.masterKnob.onValueChange = [this] {
        engine.setMaster((float)topBar.masterKnob.getValue());
    };
    topBar.phonesKnob.onValueChange = [this] {
        engine.setPhonesLevel((float)topBar.phonesKnob.getValue());
    };
    const char* mixNames[3] = { "XFADE", "MASTER", "BLEND" };
    juce::Slider* mixSliders[3] = { &xfade, &cueBlend, &cueBlend };
    for (int i = 0; i < 3; ++i) {
        if (i == 1)
            continue; // MASTER lives in the top bar now
        addAndMakeVisible(mixerCaps[i]);
        mixerCaps[i].setText(mixNames[i], juce::dontSendNotification);
        mixerCaps[i].setJustificationType(juce::Justification::centred);
        mixerCaps[i].setColour(juce::Label::textColourId, DJLook::dimInk);
        mixerCaps[i].setFont(
            juce::Font(juce::FontOptions{}.withHeight(11.0f)).boldened());
        addAndMakeVisible(*mixSliders[i]);
    }
    auto armGlobal = [this](juce::Slider& sl, lean::MidiTarget t) {
        sl.onDragStart = [this, t] {
            if (learnButton.getToggleState()) {
                pending = PendingLearn{ 0, t };
                sysStatus.setText("Move a MIDI control to bind...",
                                  juce::dontSendNotification);
            }
        };
    };
    armGlobal(xfade, lean::MidiTarget::Xfade);
    armGlobal(topBar.masterKnob, lean::MidiTarget::Master);
    armGlobal(topBar.phonesKnob, lean::MidiTarget::Phones);
    armGlobal(cueBlend, lean::MidiTarget::Blend);

    addAndMakeVisible(folderButton);
    addAndMakeVisible(toA);
    addAndMakeVisible(toB);
    addAndMakeVisible(folderLabel);
    addAndMakeVisible(libraryHeader);
    libraryHeader.setText("LIBRARY", juce::dontSendNotification);
    libraryHeader.setFont(juce::Font(juce::FontOptions{}.withHeight(15.0f)).boldened());
    libraryHeader.setColour(juce::Label::textColourId, DJLook::accent);
    addAndMakeVisible(browserGlass);
    browserGlass.toBack();
    addAndMakeVisible(searchBox);
    addAndMakeVisible(fileList);
    folderButton.onClick = [this] { openFolder(); };
    toA.setRadioGroupId(100);
    toB.setRadioGroupId(100);
    toA.setClickingTogglesState(true);
    toB.setClickingTogglesState(true);
    toA.setToggleState(true, juce::dontSendNotification);
    folderLabel.setText("No folder", juce::dontSendNotification);
    searchBox.setTextToShowWhenEmpty("Search indexed library...",
                                     juce::Colours::grey);
    // Debounced search: restart a short timer per keystroke so the SQLite
    // query runs after typing pauses, not on every character (review S6).
    searchDebounce.owner = this;
    searchBox.onTextChange = [this] { searchDebounce.startTimer(150); };
    fileModel.onDoubleClick = [this](int row) { loadBrowserRow(row); };
    // Auto-play and header refresh once an async load commits (review S5).
    engine.onDeckReady = [this](int deck) {
        engine.play(deck, true);
        updateDeckHeader(deck);
    };
    // Warm the VST3 cache off the message thread before the FX menu opens
    // (review S9).
    engine.refreshFxScan();

    addAndMakeVisible(midiCombo);
    addAndMakeVisible(learnButton);
    addAndMakeVisible(sysStatus);
    addAndMakeVisible(topBar);
    midiCombo.addItem("(no MIDI)", 1);
    for (const auto& name : engine.midiInputNames())
        midiCombo.addItem(name, midiCombo.getNumItems() + 1);
    midiCombo.setSelectedId(1, juce::dontSendNotification);
    midiCombo.onChange = [this] { openMidiDevice(midiCombo.getSelectedId() - 2); };
    learnButton.setClickingTogglesState(true);
    learnButton.onClick = [this] {
        if (!learnButton.getToggleState())
            pending.reset();
        else
            sysStatus.setText("Touch a control, then move a MIDI CC...",
                              juce::dontSendNotification);
    };
    topBar.setupBtn.onClick = [this] { openSettings(); };
    topBar.recTab.onClick = [this] {
        juce::String status;
        engine.toggleRecord(status);
        sysStatus.setText(status, juce::dontSendNotification);
    };
    topBar.mixTab.onClick = [this] { showBrowser(true); };
    topBar.fxTab.onClick = [this] { showBrowser(false); };
    topBar.fxTab.setEnabled(true);
    topBar.fxTab.setTooltip("");

    addAndMakeVisible(fxPage);
    const char* auxNames[4] = { "AUX 1", "AUX 2", "AUX 3", "AUX 4" };
    for (int a = 0; a < 4; ++a) {
        auto& s = auxStrips[a];
        fxPage.addAndMakeVisible(s.header);
        s.header.setText(auxNames[a], juce::dontSendNotification);
        s.header.setJustificationType(juce::Justification::centred);
        s.header.setFont(
            juce::Font(juce::FontOptions{}.withHeight(14.0f)).boldened());
        s.header.setColour(juce::Label::textColourId, DJLook::accent);
        fxPage.addAndMakeVisible(s.slot);
        fxPage.addAndMakeVisible(s.bypass);
        fxPage.addAndMakeVisible(s.edit);
        fxPage.addAndMakeVisible(s.sendA);
        fxPage.addAndMakeVisible(s.sendB);
        fxPage.addAndMakeVisible(s.ret);
        for (auto* cap : { &s.capA, &s.capB, &s.capR }) {
            fxPage.addAndMakeVisible(cap);
            cap->setJustificationType(juce::Justification::centred);
            cap->setColour(juce::Label::textColourId, DJLook::dimInk);
            cap->setFont(
                juce::Font(juce::FontOptions{}.withHeight(11.0f)).boldened());
        }
        s.slot.onClick = [this, a] { pickAuxPlugin(a); };
        s.edit.setEnabled(false);        s.bypass.setClickingTogglesState(true);
        s.bypass.onClick = [this, a] {
            engine.setAuxBypass(a, auxStrips[a].bypass.getToggleState());
        };
        s.edit.onClick = [this, a] { engine.showAuxEditor(a); };
        s.sendA.setRange(0.0, 1.0, 0.001);
        s.sendA.onValueChange = [this, a] {
            engine.setSend(0, a, (float)auxStrips[a].sendA.getValue());
        };
        s.sendB.setRange(0.0, 1.0, 0.001);
        s.sendB.onValueChange = [this, a] {
            engine.setSend(1, a, (float)auxStrips[a].sendB.getValue());
        };
        s.ret.setRange(0.0, 1.25, 0.001);
        s.ret.setValue(1.0);
        s.ret.onValueChange = [this, a] {
            engine.setAuxReturn(a, (float)auxStrips[a].ret.getValue());
        };
    }
    fxPage.setVisible(false);
    sysStatus.setText("MIDI: none", juce::dontSendNotification);
    addAndMakeVisible(mixerGlass);
    mixerGlass.toBack();
    addAndMakeVisible(mixerHeader);
    mixerHeader.setText("MIXER", juce::dontSendNotification);
    mixerHeader.setJustificationType(juce::Justification::centred);
    mixerHeader.setFont(juce::Font(juce::FontOptions{}.withHeight(15.0f)).boldened());
    mixerHeader.setColour(juce::Label::textColourId, DJLook::accent);
    const char* chanNames[2] = { "A", "B" };
    for (int d = 0; d < 2; ++d) {
        addAndMakeVisible(chanCaps[d]);
        addAndMakeVisible(chanMeters[d]);
        chanCaps[d].setText(chanNames[d], juce::dontSendNotification);
        chanCaps[d].setJustificationType(juce::Justification::centred);
        chanCaps[d].setColour(juce::Label::textColourId, DJLook::accent);
        chanCaps[d].setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)).boldened());
    }
    loadMidiMap();
    // Replay the persisted map into the engine's realtime grid (+ the FLX4
    // table once a matching device is opened; loadSettings does that).
    applyMidiRouting();
    loadSettings();

    setSize(1100, 824);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    saveSettings(); // W9: capture browser scroll + per-file zoom at exit
    setLookAndFeel(nullptr);
}

void MainComponent::applyEq(int deck) {
    const auto& s = strips[deck];
    engine.setEq(deck, s.low.getValue(), s.mid.getValue(), s.high.getValue());
}

void MainComponent::SearchDebounce::timerCallback() {
    stopTimer(); // one-shot: fires once typing pauses
    if (owner)
        owner->refreshBrowserList();
}

void MainComponent::updateDeckHeader(int deck) {
    const double bpm = engine.getBpm(deck);
    juce::String text = deck == 0 ? "DECK A" : "DECK B";
    if (engine.hasFile(deck))
        text += bpm > 0.0 ? " | " + juce::String(bpm, 1) + " BPM" : " | -- BPM";
    strips[deck].header.setText(text, juce::dontSendNotification);
}

juce::Slider* MainComponent::sliderFor(int deck, lean::MidiTarget target) {
    switch (target) {
        case lean::MidiTarget::Tempo: return &strips[deck & 1].tempo;
        case lean::MidiTarget::Gain: return &strips[deck & 1].gain;
        case lean::MidiTarget::Fader: return &strips[deck & 1].fader;
        case lean::MidiTarget::Low: return &strips[deck & 1].low;
        case lean::MidiTarget::Mid: return &strips[deck & 1].mid;
        case lean::MidiTarget::High: return &strips[deck & 1].high;
        case lean::MidiTarget::Xfade: return &xfade;
        case lean::MidiTarget::Master: return &topBar.masterKnob;
        case lean::MidiTarget::Blend: return &cueBlend;
        case lean::MidiTarget::Phones: return &topBar.phonesKnob;
        // Send rides the per-deck AUX knob; learn reflection uses the
        // channel strip's send slider via the FLX4 path only.
        case lean::MidiTarget::Send: return nullptr;
        case lean::MidiTarget::None: return nullptr;
    }
    return nullptr;
}

juce::File MainComponent::midiMapFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("TRANSIT/midimap.txt");
}

void MainComponent::loadMidiMap() {
    const auto f = midiMapFile();
    if (f.existsAsFile())
        midiMap.load(f.loadFileAsString().toStdString());
}

void MainComponent::saveMidiMap() {
    const auto f = midiMapFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(midiMap.save());
}

void MainComponent::applyMidiRouting() {
    engine.clearMidiGrid();
    for (const auto& b : midiMap.all())
        engine.bindMidi(b.channel, b.cc, b.deck, b.target);
    // Installs the FLX4 value table (14-bit pairs) when a matching device
    // is open; a plain clear+relearn above keeps learned binds intact.
    engine.setFlx4Mode(flx4Active);
}

void MainComponent::openMidiDevice(int engineIndex) {
    engine.openMidiInput(engineIndex);
    flx4Active = false;
    shiftHeld[0] = shiftHeld[1] = false;
    for (int d = 0; d < 2; ++d)
        engine.setBend(d, 0.0);
    const auto names = engine.midiInputNames();
    if (engineIndex >= 0 && engineIndex < names.size()) {
        const juce::String name = names[engineIndex];
        uiState.midi = name.toStdString();
        if (lean::flx4NameMatches(name.toRawUTF8())) {
            flx4Active = true;
            sysStatus.setText("MIDI: DDJ-FLX4 mapped", juce::dontSendNotification);
        } else {
            sysStatus.setText("MIDI: " + name, juce::dontSendNotification);
        }
    } else {
        uiState.midi.clear();
        sysStatus.setText("MIDI: none", juce::dontSendNotification);
    }
    applyMidiRouting();
    if (flx4Active)
        engine.sendFlx4SysEx(lean::kFlx4ResyncSysEx,
                             (int)sizeof(lean::kFlx4ResyncSysEx));
    // LED cache invalidated; the next timer tick resends every LED.
    for (int d = 0; d < 2; ++d)
        ledPlay[d] = ledCue[d] = ledSync[d] = ledPfl[d] = -1;
}

void MainComponent::setSyncFromUi(int deck, bool want) {
    if (want && !engine.setSync(deck, true)) {
        strips[deck].sync.setToggleState(false, juce::dontSendNotification);
        sysStatus.setText("SYNC needs analyzed BPM on both decks",
                          juce::dontSendNotification);
        return;
    }
    if (!want)
        engine.setSync(deck, false);
    strips[deck].sync.setToggleState(want, juce::dontSendNotification);
}

void MainComponent::drainFlx4Actions() {
    lean::Flx4ActionMsg msgs[16];
    const int n = engine.drainFlx4Actions(msgs, 16);
    for (int i = 0; i < n; ++i) {
        const auto& m = msgs[i];
        const auto action = (lean::Flx4Action)m.action;
        const int d = m.deck & 1;
        switch (action) {
            case lean::Flx4Action::Play:
                if (m.pressed != 0) {
                    const bool want = !engine.isPlaying(d);
                    engine.play(d, want);
                    strips[d].play.setToggleState(want,
                                                  juce::dontSendNotification);
                }
                break;
            case lean::Flx4Action::Cue:
                if (m.pressed != 0)
                    engine.startCuePreview(d);
                else
                    engine.stopCuePreview(d);
                break;
            case lean::Flx4Action::Sync:
                if (m.pressed != 0)
                    setSyncFromUi(d, !engine.isSync(d));
                break;
            case lean::Flx4Action::Pfl:
                if (m.pressed != 0) {
                    const bool want = !engine.isPfl(d);
                    engine.setPfl(d, want);
                    strips[d].pfl.setToggleState(want,
                                                 juce::dontSendNotification);
                }
                break;
            case lean::Flx4Action::Shift:
                shiftHeld[d] = m.pressed != 0;
                break;
            case lean::Flx4Action::Load:
                if (m.pressed != 0) {
                    toB.setToggleState(d == 1, juce::dontSendNotification);
                    loadBrowserRow(fileList.getSelectedRow());
                }
                break;
            case lean::Flx4Action::BrowsePress:
                if (m.pressed != 0)
                    loadBrowserRow(fileList.getSelectedRow());
                break;
            case lean::Flx4Action::BrowseScroll: {
                const int maxRow
                    = juce::jmax(0, (int)fileModel.rows.size() - 1);
                const int row = juce::jlimit(
                    0, maxRow, fileList.getSelectedRow() + m.delta);
                fileList.selectRow(row);
                break;
            }
            case lean::Flx4Action::JogNudge:
                engine.setBend(d, juce::jlimit(-1.0, 1.0, (double)m.delta / 32.0));
                jogIdleTicks[d] = 0;
                break;
            case lean::Flx4Action::JogSearch:
                engine.jumpBeats(d, (double)juce::jlimit(-16, 16, (int)m.delta));
                break;
            case lean::Flx4Action::None:
                break;
        }
    }
}

void MainComponent::updateFlx4Leds() {
    if (!flx4Active)
        return;
    const int chs[2] = { (int)lean::Flx4Leds::deck1Ch, (int)lean::Flx4Leds::deck2Ch };
    for (int d = 0; d < 2; ++d) {
        // CDJ lamp semantics: play lamp off during cue-preview hold (the
        // deck is playing but the user is holding CUE); cue lamp lit while
        // paused AND while holding preview.
        const bool preview = engine.isPreviewing(d);
        const bool playing = engine.isPlaying(d) && !preview;
        const int play = playing ? 1 : 0;
        const int cue = (!engine.isPlaying(d) || preview) ? 1 : 0;
        const int sync = engine.isSync(d) ? 1 : 0;
        const int pfl = engine.isPfl(d) ? 1 : 0;
        if (play != ledPlay[d]) {
            ledPlay[d] = play;
            engine.sendFlx4Note(chs[d], lean::Flx4Leds::play, play != 0);
        }
        if (cue != ledCue[d]) {
            ledCue[d] = cue;
            engine.sendFlx4Note(chs[d], lean::Flx4Leds::cue, cue != 0);
        }
        if (sync != ledSync[d]) {
            ledSync[d] = sync;
            engine.sendFlx4Note(chs[d], lean::Flx4Leds::sync, sync != 0);
        }
        if (pfl != ledPfl[d]) {
            ledPfl[d] = pfl;
            engine.sendFlx4Note(chs[d], lean::Flx4Leds::pfl, pfl != 0);
        }
    }
}

// F-key transport: index 0..7 maps to deck (index & 1) and action
// (index >> 1): play, cue hold, PFL, load selection.
namespace {
int fnKeyIndex(int keyCode) {
    if (keyCode == juce::KeyPress::F1Key)
        return 0;
    if (keyCode == juce::KeyPress::F2Key)
        return 1;
    if (keyCode == juce::KeyPress::F3Key)
        return 2;
    if (keyCode == juce::KeyPress::F4Key)
        return 3;
    if (keyCode == juce::KeyPress::F5Key)
        return 4;
    if (keyCode == juce::KeyPress::F6Key)
        return 5;
    if (keyCode == juce::KeyPress::F7Key)
        return 6;
    if (keyCode == juce::KeyPress::F8Key)
        return 7;
    return -1;
}
} // namespace

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    const int idx = fnKeyIndex(key.getKeyCode());
    if (idx < 0)
        return false;
    if (fnHeld[idx])
        return true; // auto-repeat: the action already ran on first press
    fnHeld[idx] = true;
    const int d = idx & 1;
    switch (idx >> 1) {
        case 0:
            strips[d].play.triggerClick();
            break;
        case 1:
            engine.startCuePreview(d);
            break;
        case 2:
            strips[d].pfl.triggerClick();
            break;
        case 3:
            toB.setToggleState(d == 1, juce::dontSendNotification);
            loadBrowserRow(fileList.getSelectedRow());
            break;
        default:
            break;
    }
    return true;
}

bool MainComponent::keyReleased(const juce::KeyPress& key) {
    const int idx = fnKeyIndex(key.getKeyCode());
    if (idx < 0)
        return false;
    fnHeld[idx] = false;
    const int d = idx & 1;
    if ((idx >> 1) == 1 && engine.isPreviewing(d))
        engine.stopCuePreview(d);
    return true;
}

void MainComponent::openFile(int deck) {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Open audio file", browserDir, "*.wav;*.mp3;*.flac;*.ogg;*.m4a;*.aac;*.mp4");
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, deck, chooser](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            // Async: the status line follows engine.deckState() from the
            // 30 Hz timer (Loading -> filename or error).
            requestLoad(deck, file.getFullPathName());
        });
}

void MainComponent::openSettings() {
    auto dialog = std::make_unique<SettingsDialog>(
        engine, showBpm,
        [this] {
            refreshBrowserList();
            sysStatus.setText("Library index cleared", juce::dontSendNotification);
        },
        [this] {
            midiMap = lean::MidiMap();
            saveMidiMap();
            sysStatus.setText("MIDI map reset", juce::dontSendNotification);
        },
        [this](bool sync) { setBeatModeUI(sync); },
        [this](bool show) { showBpm = show; },
        [this] { saveSettings(); });
    dialog->setSize(420, 500);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(dialog.release());
    opts.dialogTitle = "Setup";
    opts.dialogBackgroundColour = DJLook::bg;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.componentToCentreAround = this;
    opts.launchAsync();
}

void MainComponent::setBeatModeUI(bool syncMode) {
    engine.setBeatMode(syncMode ? AudioEngine::BeatMode::Sync
                                : AudioEngine::BeatMode::Free);
    for (int d = 0; d < 2; ++d) {
        strips[d].sync.setVisible(syncMode);
        if (!syncMode)
            strips[d].sync.setToggleState(false, juce::dontSendNotification);
    }
    sysStatus.setText(syncMode ? "Sync mode: SYNC locks tempo, jumps hold phase"
                               : "Free mode: CDJ-style, no sync",
                      juce::dontSendNotification);
}

juce::File MainComponent::settingsFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("TRANSIT/settings.txt");
}

void MainComponent::loadSettings() {
    showBpm = true;
    const auto f = settingsFile();
    bool sync = false;
    bool eco = false;
    juce::StringArray auxPaths;
    if (f.existsAsFile()) {
        const auto text = f.loadFileAsString();
        // W8/W9: restore the per-deck zoom index, the browser top row and
        // the per-file zoom memory (unknown or malformed values keep the
        // defaults).
        uiState = lean::parseUiState(text.toStdString());
        pendingScroll = uiState.scroll;
        suppressZoomSave = true;
        for (int d = 0; d < 2; ++d)
            strips[d].wave->setZoomIndex(uiState.zoom[d]);
        suppressZoomSave = false;
        for (const auto& ln : juce::StringArray::fromLines(text)) {
            if (ln.startsWith("mode="))
                sync = ln.substring(5).trim() == "sync";
            else if (ln.startsWith("showBpm="))
                showBpm = ln.substring(8).trim() != "0";
            else if (ln.startsWith("keyEco="))
                eco = ln.substring(7).trim() != "0";
            else if (ln.startsWith("aux"))
                auxPaths.add(ln.fromFirstOccurrenceOf("=", false, false).trim());
        }
    }
    engine.setKeylockEco(eco);
    setBeatModeUI(sync);
    // FLX4 round 5: restore the last-selected MIDI device (exact name match).
    if (!uiState.midi.empty()) {
        const juce::String want(uiState.midi);
        for (int i = 0; i < midiCombo.getNumItems(); ++i) {
            if (midiCombo.getItemText(i) == want) {
                midiCombo.setSelectedId(i + 1, juce::dontSendNotification);
                openMidiDevice(i - 1); // item 0 = "(no MIDI)" -> engine -1
                break;
            }
        }
    }
    // Reload persisted aux plugins (slow ones cost startup time; failures
    // only land in the status line, never crash startup).
    for (int a = 0; a < 4 && a < auxPaths.size(); ++a) {
        if (auxPaths[a].isNotEmpty()) {
            juce::String error;
            if (engine.loadAuxPlugin(a, auxPaths[a], error)) {
                auxStrips[a].slot.setButtonText(engine.auxName(a));
                auxStrips[a].edit.setEnabled(engine.auxHasEditor(a));
            } else {
                sysStatus.setText("AUX " + juce::String(a + 1) + ": " + error,
                                  juce::dontSendNotification);
            }
        }
    }
}

void MainComponent::saveSettings() {
    juce::String text;
    text += juce::String("mode=")
        + (engine.beatMode() == AudioEngine::BeatMode::Sync ? "sync" : "free")
        + "\n";
    text += juce::String("showBpm=") + (showBpm ? "1" : "0") + "\n";
    text += juce::String("keyEco=") + (engine.keylockEco() ? "1" : "0") + "\n";
    for (int a = 0; a < 4; ++a)
        text += "aux" + juce::String(a) + "=" + engine.auxPath(a) + "\n";
    // W8/W9: cross-session UI state — live zoom per deck, live browser
    // scroll (permille of the list's scroll range), and the accumulated
    // per-file zoom memory.
    lean::UiState ui = uiState;
    for (int d = 0; d < 2; ++d)
        ui.zoom[d] = strips[d].wave ? strips[d].wave->getZoomIndex() : 3;
    ui.scroll = juce::jlimit(
        0, 1000, (int)(fileList.getVerticalPosition() * 1000.0 + 0.5));
    text += juce::String(lean::serializeUiState(ui));
    const auto f = settingsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(text);
}

void MainComponent::showBrowser(bool show) {
    juce::Component* browserWidgets[] = {
        &folderButton, &toA, &toB, &folderLabel, &libraryHeader, &searchBox,
        &browserGlass, &fileList, &midiCombo, &learnButton, &sysStatus
    };
    for (auto* w : browserWidgets)
        w->setVisible(show);
    fxPage.setVisible(!show);
}

void MainComponent::pickAuxPlugin(int aux) {
    juce::PopupMenu menu;
    menu.addItem("Unload", false, false, [this, aux] {
        engine.unloadAuxPlugin(aux);
        auxStrips[aux].slot.setButtonText("Empty");
        auxStrips[aux].edit.setEnabled(false);
        saveSettings();
    });
    menu.addSeparator();
    // Cached discovery (review S9): the walk already ran on the worker;
    // kick a background refresh so newly installed plugins show next time.
    const auto found = engine.fxPlugins();
    engine.refreshFxScan();
    int id = 1;
    for (const auto& entry : found) {
        const auto display = entry.upToFirstOccurrenceOf("|", false, false);
        const auto path = entry.fromFirstOccurrenceOf("|", false, false);
        menu.addItem(display, true, false, [this, aux, path, display] {
            juce::String error;
            if (engine.loadAuxPlugin(aux, path, error)) {
                auxStrips[aux].slot.setButtonText(display);
                auxStrips[aux].edit.setEnabled(engine.auxHasEditor(aux));
                sysStatus.setText("AUX " + juce::String(aux + 1) + ": " + display,
                                  juce::dontSendNotification);
                saveSettings();
            } else {
                sysStatus.setText("AUX load failed: " + error,
                                  juce::dontSendNotification);
            }
        });
        if (++id > 200)
            break; // sanity cap on menu size
    }
    if (found.isEmpty()) {
        const bool scanning = engine.fxScanRunning();
        menu.addItem(scanning ? "(scanning for VST3...)" : "(no VST3 found)",
                     false, false, [] {});
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withParentComponent(this));
}

void MainComponent::openFolder() {
    auto chooser = std::make_shared<juce::FileChooser>("Browse music folder");
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser](const juce::FileChooser& fc) {
            const auto dir = fc.getResult();
            if (dir == juce::File())
                return;
            browserDir = dir;
            indexedFolderLabel = dir.getFullPathName();
            engine.indexFolder(dir.getFullPathName()); // async scan
            scanNoteActive = true;
            folderLabel.setText(indexedFolderLabel + " - scanning...",
                                juce::dontSendNotification);
            searchBox.setText("", juce::dontSendNotification);
            refreshBrowserList();
        });
}

void MainComponent::refreshBrowserList() {
    juce::StringArray paths;
    const auto q = searchBox.getText().trim();
    if (q.isNotEmpty()) {
        for (const auto& p : engine.searchLibrary(q.toStdString()))
            paths.add(juce::String(p));
    } else if (browserDir != juce::File()) {
        for (const auto& ext : { "*.wav", "*.mp3", "*.flac", "*.ogg",
                                "*.m4a", "*.aac", "*.mp4" }) {
            for (const auto& f : browserDir.findChildFiles(
                     juce::File::findFiles, false, ext))
                paths.add(f.getFullPathName());
        }
        paths.sort(true);
    }
    // G6: precompute display metadata once per refresh; paint is I/O-free.
    // Round 2 adds the tempo estimate and duration: the tempo comes from the
    // library index (analysed at scan time), the duration from the index or
    // from a header-only probe when the track was never indexed.
    std::vector<std::string> pathStd;
    pathStd.reserve((size_t)paths.size());
    for (const auto& p : paths)
        pathStd.push_back(p.toStdString());
    std::map<std::string, lean::Library::TrackMeta> indexed;
    for (const auto& kv : engine.libraryMetadata(pathStd))
        indexed.emplace(kv.first, kv.second);

    fileModel.rows.clear();
    fileModel.rows.reserve((size_t)paths.size());
    for (int i = 0; i < paths.size(); ++i) {
        const juce::File f(paths[i]);
        FileList::Row row;
        row.path = paths[i];
        row.name = f.getFileName();
        row.folder = f.getParentDirectory().getFileName();
        row.size = juce::File::descriptionOfSizeInBytes(f.getSize());
        row.date = f.getLastModificationTime().formatted("%Y-%m-%d");
        const auto it = indexed.find(row.path.toStdString());
        if (it != indexed.end()) {
            if (it->second.bpm > 0.0)
                row.bpm = juce::String(it->second.bpm, 1);
            if (it->second.seconds > 0.0)
                row.time = formatTime(it->second.seconds);
        }
        if (row.bpm.isEmpty())
            row.bpm = juce::String::charToString(0x2014); // em dash: tempo unknown
        if (row.time.isEmpty())
            row.time = probeDuration(f);
        fileModel.rows.push_back(std::move(row));
    }
    fileList.updateContent();
    // W9: apply the stored list position once, after the first real
    // population (an empty refresh keeps it pending for the next one).
    // setVerticalPosition is a no-op when every row already fits.
    if (pendingScroll >= 0 && !fileModel.rows.empty()) {
        fileList.setVerticalPosition((double)pendingScroll / 1000.0);
        pendingScroll = -1;
    }
}

juce::String MainComponent::probeDuration(const juce::File& file) {
    // Header-only read: no sample data is decoded, no index is written.
    std::unique_ptr<juce::AudioFormatReader> reader(
        audioFormats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return {};
    return formatTime((double)reader->lengthInSamples / reader->sampleRate);
}

void MainComponent::loadBrowserRow(int row) {
    if (row < 0 || row >= (int)fileModel.rows.size())
        return;
    const int deck = toB.getToggleState() ? 1 : 0;
    const juce::File file(fileModel.rows[(size_t)row].path);
    requestLoad(deck, file.getFullPathName()); // async; status via timer
}

void MainComponent::requestLoad(int deck, const juce::String& path) {
    deckPath[deck & 1] = {};
    // Applying a stored zoom must not be mistaken for a user zoom change
    // (it would re-record the entry it just read).
    suppressZoomSave = true;
    if (const int* z = lean::findFileZoom(uiState, path.toStdString()))
        strips[deck & 1].wave->setZoomIndex(*z);
    suppressZoomSave = false;
    deckPath[deck & 1] = path;
    engine.loadFile(deck, path);
}

void MainComponent::rememberFileZoom(int deck) {
    const juce::String& p = deckPath[deck & 1];
    if (p.isEmpty() || !strips[deck & 1].wave)
        return;
    lean::putFileZoom(uiState, p.toStdString(),
                      strips[deck & 1].wave->getZoomIndex());
}

juce::String MainComponent::formatTime(double seconds) const {
    const int total = (int)seconds;
    return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}

void MainComponent::dumpLayout() const {
    std::printf("layout content %dx%d\n", getWidth(), getHeight());
    std::printf("layout topbar %d,%d %dx%d\n", topBar.getX(), topBar.getY(),
                topBar.getWidth(), topBar.getHeight());
    for (int d = 0; d < 2; ++d) {
        const auto& s = strips[d];
        std::printf("layout deck%d wave %d,%d %dx%d fader %d,%d %dx%d\n", d,
                    s.wave->getX(), s.wave->getY(), s.wave->getWidth(),
                    s.wave->getHeight(), s.fader.getX(), s.fader.getY(),
                    s.fader.getWidth(), s.fader.getHeight());
    }
    std::printf("layout xfade %d,%d %dx%d filelist %d,%d %dx%d\n",
                xfade.getX(), xfade.getY(), xfade.getWidth(), xfade.getHeight(),
                fileList.getX(), fileList.getY(), fileList.getWidth(),
                fileList.getHeight());
    std::printf("layout fxpage %d,%d %dx%d visible=%d\n", fxPage.getX(),
                fxPage.getY(), fxPage.getWidth(), fxPage.getHeight(),
                (int)fxPage.isVisible());
    const auto& a0 = auxStrips[0];
    std::printf("layout aux0 slot %d,%d %dx%d sendA %d,%d %dx%d\n",
                a0.slot.getX(), a0.slot.getY(), a0.slot.getWidth(),
                a0.slot.getHeight(), a0.sendA.getX(), a0.sendA.getY(),
                a0.sendA.getWidth(), a0.sendA.getHeight());
}

void MainComponent::timerCallback() {
    float masterPeak = 0.0f, chA = 0.0f, chB = 0.0f;
    engine.takePeaks(masterPeak, chA, chB);
    meterShown = std::max(DJLook::meterUnit(masterPeak),
                          meterShown - kMeterFall);
    meterHold.push(meterShown);
    topBar.pushMaster(masterPeak);
    engine.pumpFx();
    if (engine.fxRateDirty())
        engine.prepareFx(engine.currentSampleRate());
    topBar.recTab.setToggleState(engine.isRecording(),
                                 juce::dontSendNotification);
    topBar.clockLabel.setText(juce::Time::getCurrentTime().formatted("%H:%M"),
                              juce::dontSendNotification);
    const float chPeak[2] = { chA, chB };
    ++statusAnimTick; // G5: one animation step per tick, shared by both decks
    auto reflect = [](juce::Slider& sl, double v) {
        if (!sl.isMouseButtonDown() && std::abs(sl.getValue() - v) > 1e-4)
            sl.setValue(v, juce::dontSendNotification);
    };
    // MIDI learn: bind the most recent CC to the armed control.
    if (learnButton.getToggleState() && pending.has_value()) {
        const uint64_t stamp = engine.midiLearnStamp();
        if (stamp != learnSeen) {
            learnSeen = stamp;
            int ch = 0, cc = 0;
            engine.midiLearnEvent(ch, cc);
            midiMap.bind(ch, cc, pending->deck, pending->target);
            engine.bindMidi(ch, cc, pending->deck, pending->target);
            saveMidiMap();
            sysStatus.setText("Bound ch " + juce::String(ch) + " CC "
                                  + juce::String(cc),
                              juce::dontSendNotification);
            pending.reset();
            learnButton.setToggleState(false, juce::dontSendNotification);
        }
    }
    engine.updateSync();
    for (int d = 0; d < 2; ++d) {
        // Reflect audio-side control state (MIDI moves, sync chase).
        reflect(strips[d].tempo, engine.getTempo(d));
        reflect(strips[d].gain, engine.getGain(d));
        reflect(strips[d].fader, engine.getFader(d));
        double lo = 0.0, mi = 0.0, hi = 0.0;
        engine.getEq(d, lo, mi, hi);
        reflect(strips[d].low, lo);
        reflect(strips[d].mid, mi);
        reflect(strips[d].high, hi);
        chanShown[d] = std::max(DJLook::meterUnit(chPeak[d]),
                                chanShown[d] - kMeterFall);
        chanMeters[d].setLevel(chanShown[d]);
        strips[d].time.setText(
            formatTime(engine.getPositionSeconds(d)) + " / "
                + formatTime(engine.getLengthSeconds(d)),
            juce::dontSendNotification);
        strips[d].play.setToggleState(engine.isPlaying(d),
                                      juce::dontSendNotification);
        strips[d].sync.setToggleState(engine.isSync(d),
                                      juce::dontSendNotification);
        if (engine.isSync(d))
            strips[d].tempo.setValue(engine.getTempo(d),
                                     juce::dontSendNotification);
        // Async load pipeline -> dedicated status row (review S5/G5): a
        // state chip plus a payload label; each updates only when its text
        // actually changes (the colour setter early-outs the same way).
        juce::String chip = "EMPTY";
        juce::Colour chipColour = DJLook::dimInk;
        juce::String payload;
        juce::Colour payloadColour = DJLook::dimInk;
        switch (static_cast<lean::LoadState>(engine.deckState(d))) {
            case lean::LoadState::Empty:
                break;
            case lean::LoadState::Loading:
                chip = "LOADING";
                for (int i = 0; i <= (statusAnimTick / 10) % 3; ++i)
                    chip += '.';
                chipColour = DJLook::cyan;
                break;
            case lean::LoadState::Analyzing:
                chip = "ANALYZING";
                chipColour = DJLook::cyan;
                break;
            case lean::LoadState::Ready:
                chip = "READY";
                chipColour = DJLook::accent;
                payload = engine.deckName(d);
                payloadColour = DJLook::ink;
                break;
            case lean::LoadState::Error:
                chip = "ERROR";
                chipColour = DJLook::alarm;
                payload = engine.deckError(d);
                payloadColour = DJLook::alarm;
                break;
        }
        if (chip != strips[d].stateChip.getText())
            strips[d].stateChip.setText(chip, juce::dontSendNotification);
        strips[d].stateChip.setColour(juce::Label::textColourId, chipColour);
        if (payload != strips[d].status.getText())
            strips[d].status.setText(payload, juce::dontSendNotification);
        strips[d].status.setColour(juce::Label::textColourId, payloadColour);
        // Live effective tempo: analyzed BPM follows the pitch fader, unless
        // the user hid it in Free mode (Sync always shows it).
        juce::String text = d == 0 ? "DECK A" : "DECK B";
        const bool show = showBpm
            || engine.beatMode() == AudioEngine::BeatMode::Sync;
        if (engine.hasFile(d) && show) {
            const double eff = engine.getBpm(d) * engine.getTempo(d);
            text += eff > 0.0 ? " | " + juce::String(eff, 1) + " BPM"
                              : " | -- BPM";
        }
        if (text != lastHeader[d]) {
            lastHeader[d] = text;
            strips[d].header.setText(text, juce::dontSendNotification);
        }
    }
    reflect(xfade, engine.getXfade());
    reflect(topBar.masterKnob, engine.getMaster());
    reflect(topBar.phonesKnob, engine.getPhonesLevel());
    reflect(cueBlend, engine.getBlend());
    // Async folder scan feedback: finalize the label (and refresh an active
    // search result) once the worker reports no queued index job left.
    if (scanNoteActive && !engine.scanActive()) {
        scanNoteActive = false;
        const int n = engine.scanCount();
        folderLabel.setText(n >= 0 ? indexedFolderLabel + " - "
                                         + juce::String(n) + " indexed"
                                   : indexedFolderLabel,
                            juce::dontSendNotification);
        refreshBrowserList();
    }
    // G8 (finish): targeted invalidation instead of one whole-window
    // repaint — output-meter well every tick; each deck waveform only when
    // a value it paints changed (load state or quantized playhead token:
    // seek-while-paused still repaints, an idle loaded waveform stops
    // re-rendering every tick; zoom changes repaint themselves, W4).
    // Everything else in this callback already repaints itself: labels,
    // sliders, tabs, channel VUs, the top-bar master strip.
    for (int d = 0; d < 2; ++d) {
        const int st = engine.deckState(d);
        // Quantized change token, not a measurement: truncation at 1 ns is
        // irrelevant and keeps the comparison on integers.
        const int64_t tok = (int64_t)(engine.getPositionSeconds(d) * 1.0e9);
        if (st != waveStateSeen[d] || tok != wavePosSeen[d]) {
            waveStateSeen[d] = st;
            wavePosSeen[d] = tok;
            if (strips[d].wave)
                strips[d].wave->repaint();
        }
    }
    // FLX4 round 5: drain note events (transport/cue/shift/load/jog),
    // jog nudge release after 3 idle ticks (~100 ms), and LED echo.
    if (flx4Active) {
        drainFlx4Actions();
        for (int d = 0; d < 2; ++d) {
            if (jogIdleTicks[d] < 99)
                ++jogIdleTicks[d];
            if (jogIdleTicks[d] == 3)
                engine.setBend(d, 0.0);
        }
        updateFlx4Leds();
    }
    repaint(getLocalBounds().removeFromBottom(26));
}

void MainComponent::paint(juce::Graphics& g) {
    juce::ColourGradient grad(DJLook::bg.brighter(0.06f), 0.0f, 0.0f, DJLook::bg, 0.0f,
                              (float)getHeight(), false);
    g.setGradientFill(grad);
    g.fillAll();
    const auto meterArea = getLocalBounds().removeFromBottom(26).reduced(12, 7);
    g.setColour(DJLook::deep);
    g.fillRoundedRectangle(meterArea.toFloat(), 3.0f);
    const float level = juce::jlimit(0.0f, 1.0f, meterShown); // G4: dB-mapped
    if (level > 0.001f) {
        juce::ColourGradient mg(DJLook::cyan, (float)meterArea.getX(), 0.0f,
                                DJLook::alarm, (float)meterArea.getRight(), 0.0f,
                                false);
        mg.addColour(0.7, DJLook::accent);
        g.setGradientFill(mg);
        g.fillRoundedRectangle(
            meterArea.withWidth((int)((float)meterArea.getWidth() * level)).toFloat(), 3.0f);
    }
    if (meterHold.value > 0.004f) {
        const int hx = meterArea.getX()
                       + (int)(meterHold.value * (float)meterArea.getWidth());
        g.setColour(DJLook::ink);
        g.fillRect(juce::jmin(hx, meterArea.getRight() - 2), meterArea.getY(),
                   2, meterArea.getHeight());
    }
}

void MainComponent::layoutDeck(DeckStrip& s, juce::Rectangle<int> panel) {
    auto headRow = panel.removeFromTop(36);
    s.header.setBounds(headRow.removeFromLeft(headRow.getWidth() - 140));
    headRow.removeFromLeft(6);
    s.sync.setBounds(headRow.removeFromLeft(58));
    headRow.removeFromLeft(6);
    s.zoomOutBtn.setBounds(headRow.removeFromLeft(32));
    headRow.removeFromLeft(6);
    s.zoomInBtn.setBounds(headRow.removeFromLeft(32));
    auto buttons = panel.removeFromTop(56);
    s.open.setBounds(buttons.removeFromLeft(64).withSizeKeepingCentre(64, 40));
    buttons.removeFromLeft(5);
    s.play.setBounds(buttons.removeFromLeft(52).withSizeKeepingCentre(52, 40));
    buttons.removeFromLeft(5);
    s.cue.setBounds(buttons.removeFromLeft(52).withSizeKeepingCentre(52, 40));
    buttons.removeFromLeft(5);
    s.key.setBounds(buttons.removeFromLeft(52).withSizeKeepingCentre(52, 40));
    auto statusRow = panel.removeFromTop(34);
    s.stateChip.setBounds(
        statusRow.removeFromLeft(86).withSizeKeepingCentre(86, 24));
    s.status.setBounds(statusRow);
    auto timeRow = panel.removeFromTop(50);
    s.back.setBounds(timeRow.removeFromLeft(40).withSizeKeepingCentre(40, 30));
    timeRow.removeFromLeft(6);
    s.fwd.setBounds(timeRow.removeFromRight(40).withSizeKeepingCentre(40, 30));
    timeRow.removeFromRight(6);
    s.time.setBounds(timeRow);
    s.tempo.setBounds(panel.removeFromTop(64));
    // Bounded waveform band (main + overview stripe): the strip height is
    // set by resized() so the deck column never swallows the window.
    if (s.wave)
        s.wave->setBounds(panel.reduced(0, 2));
}

void MainComponent::resized() {
    // TopBar bleeds edge to edge; everything below keeps its margins.
    auto full = getLocalBounds();
    topBar.setBounds(full.removeFromTop(64));
    auto area = full.reduced(12);
    area.removeFromBottom(34); // meter
    // Proportioned deck band: the waveform strip is a bounded row inside
    // the deck (main + overview stripe), so the deck column keeps a fixed
    // content height and the library takes everything else. The clamp
    // guarantees the browser keeps its 220 px floor at every window size.
    constexpr int kStripRows = 240; // header+transport+status+time+tempo
    constexpr int kWaveCap = 190;   // main waveform + overview stripe
    const int avail = area.getHeight();
    const int cap = getLocalBounds().getHeight() >= 1200 ? 230 : kWaveCap;
    const int waveH = juce::jlimit(110, cap, avail - 8 - 220 - kStripRows - 4);
    const int deckH = kStripRows + 4 + waveH;
    auto browser = area.removeFromBottom(avail - 8 - deckH);
    area.removeFromBottom(8);

    // Club layout: deck | mixer | deck.
    constexpr int centerW = 300;
    const int deckW = (area.getWidth() - centerW - 24) / 2;
    layoutDeck(strips[0], area.removeFromLeft(deckW));
    area.removeFromLeft(12);
    auto center = area.removeFromLeft(centerW);
    area.removeFromLeft(12);
    layoutDeck(strips[1], area);

    // Mixer strip: glass panel, header, channel caps, 4 knob rows,
    // PFL + faders, xfade row pinned to the bottom (DJM-style).
    mixerGlass.setBounds(center);
    center = center.reduced(10, 8);
    mixerHeader.setBounds(center.removeFromTop(24));

    auto chanRow = center.removeFromTop(16);
    chanRow.removeFromLeft(56);
    chanCaps[0].setBounds(chanRow.removeFromLeft(chanRow.getWidth() / 2));
    chanCaps[1].setBounds(chanRow);

    juce::Slider* knobsA[4] = { &strips[0].gain, &strips[0].low, &strips[0].mid,
                                &strips[0].high };
    juce::Slider* knobsB[4] = { &strips[1].gain, &strips[1].low, &strips[1].mid,
                                &strips[1].high };
    for (int k = 0; k < 4; ++k) {
        auto row = center.removeFromTop(42);
        strips[0].knobCaps[k].setBounds(row.removeFromLeft(56));
        const int cw = row.getWidth() / 2;
        knobsA[k]->setBounds(row.removeFromLeft(cw).reduced(10, 2));
        knobsB[k]->setBounds(row.reduced(10, 2));
    }

    auto fadRow =
        center.removeFromTop(juce::jlimit(160, 320, center.getHeight() - 58));
    const int fw = fadRow.getWidth() / 2;
    auto colA = fadRow.removeFromLeft(fw);
    strips[0].pfl.setBounds(colA.removeFromTop(28).withSizeKeepingCentre(84, 24));
    chanMeters[0].setBounds(colA.removeFromLeft(14).reduced(0, 6));
    strips[0].fader.setBounds(
        colA.withSizeKeepingCentre(40, juce::jmax(120, colA.getHeight() - 36)));
    strips[1].pfl.setBounds(fadRow.removeFromTop(28).withSizeKeepingCentre(84, 24));
    chanMeters[1].setBounds(fadRow.removeFromLeft(14).reduced(0, 6));
    strips[1].fader.setBounds(
        fadRow.withSizeKeepingCentre(40, juce::jmax(120, fadRow.getHeight() - 36)));

    auto xRow = center.removeFromBottom(juce::jmin(58, center.getHeight()));
    mixerCaps[0].setBounds(xRow.removeFromLeft(56));
    xRow.removeFromLeft(4);
    auto blendCol = xRow.removeFromRight(56);
    mixerCaps[2].setBounds(blendCol.removeFromTop(14));
    cueBlend.setBounds(blendCol);
    xfade.setBounds(xRow.reduced(0, 14));

    auto brow = browser;
    browserGlass.setBounds(brow);
    auto inner = brow.reduced(12, 10);
    fxPage.setBounds(brow);
    {
        // FX view shares the browser footprint: 4 aux strips across.
        auto fx = inner;
        const int sw = fx.getWidth() / 4;
        for (int a = 0; a < 4; ++a) {
            auto& s = auxStrips[a];
            auto strip = fx.removeFromLeft(sw).reduced(8, 4);
            s.header.setBounds(strip.removeFromTop(26));
            s.slot.setBounds(strip.removeFromTop(30));
            strip.removeFromTop(4);
            auto brow2 = strip.removeFromTop(28);
            s.bypass.setBounds(brow2.removeFromLeft(70));
            brow2.removeFromLeft(6);
            s.edit.setBounds(brow2.removeFromLeft(70));
            strip.removeFromTop(4);
            auto caps = strip.removeFromTop(14);
            const int kw = caps.getWidth() / 3;
            s.capA.setBounds(caps.removeFromLeft(kw));
            s.capB.setBounds(caps.removeFromLeft(kw));
            s.capR.setBounds(caps);
            auto knobs = strip;
            s.sendA.setBounds(knobs.removeFromLeft(kw).reduced(8, 2));
            s.sendB.setBounds(knobs.removeFromLeft(kw).reduced(8, 2));
            s.ret.setBounds(knobs.reduced(8, 2));
        }
    }
    auto top = inner.removeFromTop(30);
    libraryHeader.setBounds(top.removeFromLeft(96));
    top.removeFromLeft(8);
    folderButton.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(8);
    toA.setBounds(top.removeFromLeft(60));
    top.removeFromLeft(6);
    toB.setBounds(top.removeFromLeft(60));
    top.removeFromLeft(8);
    folderLabel.setBounds(top.removeFromLeft(top.getWidth() / 2));
    searchBox.setBounds(top);
    inner.removeFromTop(6);
    auto sys = inner.removeFromTop(30);
    midiCombo.setBounds(sys.removeFromLeft(170));
    sys.removeFromLeft(8);
    learnButton.setBounds(sys.removeFromLeft(70));
    sys.removeFromLeft(8);
    sysStatus.setBounds(sys);
    fileList.setBounds(inner);
}
