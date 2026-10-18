// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "SettingsDialog.h"

SettingsDialog::SettingsDialog(AudioEngine& e, bool showBpmInit,
                               std::function<void()> onClear,
                               std::function<void()> onReset,
                               std::function<void(bool)> onMode,
                               std::function<void(bool)> onBpm,
                               std::function<void()> onSave)
    : engine(e), onLibraryCleared(std::move(onClear)),
      onMidiReset(std::move(onReset)), onModeChanged(std::move(onMode)),
      onShowBpm(std::move(onBpm)), onPersist(std::move(onSave)) {
    title.setFont(juce::Font(juce::FontOptions{}.withHeight(16.0f)).boldened());
    addAndMakeVisible(title);
    for (auto* cap : { &typeCap, &deviceCap, &rateCap, &bufferCap, &keyCap, &modeCap })
        addAndMakeVisible(cap);
    for (auto* box : { &typeBox, &deviceBox, &rateBox, &bufferBox, &keyBox, &modeBox })
        addAndMakeVisible(box);
    addAndMakeVisible(showBpmButton);
    addAndMakeVisible(statsLabel);
    addAndMakeVisible(clearLibButton);
    addAndMakeVisible(resetMidiButton);
    addAndMakeVisible(closeButton);

    refreshDevices();

    typeBox.onChange = [this] {
        const auto t = typeBox.getText();
        if (t.isNotEmpty()) {
            engine.setAudioDeviceType(t);
            refreshDevices();
        }
    };
    deviceBox.onChange = [this] {
        const auto d = deviceBox.getText();
        if (d.isNotEmpty()) {
            const auto err = engine.setOutputDevice(d);
            if (err.isNotEmpty())
                statsLabel.setText("Device error: " + err, juce::dontSendNotification);
            refreshDevices();
        }
    };
    rateBox.onChange = [this] {
        const auto r = rateBox.getText().getIntValue();
        if (r > 0)
            engine.setSampleRate((double)r);
    };
    bufferBox.onChange = [this] {
        const auto b = bufferBox.getText().getIntValue();
        if (b > 0)
            engine.setBufferSize(b);
    };
    keyBox.addItem("Full (best sound)", 1);
    keyBox.addItem("Eco (less CPU)", 2);
    keyBox.setSelectedId(engine.keylockEco() ? 2 : 1, juce::dontSendNotification);
    keyBox.onChange = [this] {
        engine.setKeylockEco(keyBox.getSelectedId() == 2);
        if (onPersist)
            onPersist();
    };

    modeBox.addItem("Free (CDJ-style, no sync)", 1);
    modeBox.addItem("Sync (tempo followers)", 2);
    modeBox.setSelectedId(
        engine.beatMode() == AudioEngine::BeatMode::Sync ? 2 : 1,
        juce::dontSendNotification);
    modeBox.onChange = [this] {
        const bool sync = modeBox.getSelectedId() == 2;
        engine.setBeatMode(sync ? AudioEngine::BeatMode::Sync
                                : AudioEngine::BeatMode::Free);
        if (onModeChanged)
            onModeChanged(sync);
        if (onPersist)
            onPersist();
    };
    showBpmButton.setToggleState(showBpmInit, juce::dontSendNotification);
    showBpmButton.onClick = [this] {
        if (onShowBpm)
            onShowBpm(showBpmButton.getToggleState());
        if (onPersist)
            onPersist();
    };

    clearLibButton.onClick = [this] {
        if (engine.clearLibrary() && onLibraryCleared)
            onLibraryCleared();
    };
    resetMidiButton.onClick = [this] {
        if (onMidiReset)
            onMidiReset();
    };
    closeButton.onClick = [this] {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };

    setSize(420, 500);
    startTimerHz(2);
}

void SettingsDialog::fillStrings(juce::ComboBox& box,
                                  const juce::StringArray& items,
                                  const juce::String& current) {
    box.clear(juce::dontSendNotification);
    int id = 1;
    int select = 0;
    for (const auto& item : items) {
        box.addItem(item, id);
        if (!current.isEmpty() && item == current)
            select = id;
        ++id;
    }
    if (select > 0)
        box.setSelectedId(select, juce::dontSendNotification);
}

void SettingsDialog::fillInts(juce::ComboBox& box, const juce::Array<int>& items,
                              int current) {
    box.clear(juce::dontSendNotification);
    int id = 1;
    int select = 0;
    for (int item : items) {
        box.addItem(juce::String(item), id);
        if (item == current)
            select = id;
        ++id;
    }
    if (select > 0)
        box.setSelectedId(select, juce::dontSendNotification);
}

void SettingsDialog::refreshDevices() {
    fillStrings(typeBox, engine.audioDeviceTypes(), engine.currentAudioDeviceType());
    fillStrings(deviceBox, engine.outputDeviceNames(), engine.currentOutputDevice());
    refreshRatesAndBuffers();
}

void SettingsDialog::refreshRatesAndBuffers() {
    fillInts(rateBox, engine.availableSampleRates(),
             (int)engine.currentSampleRate());
    fillInts(bufferBox, engine.availableBufferSizes(),
             engine.currentBufferSize());
}

void SettingsDialog::timerCallback() {
    statsLabel.setText(
        "CPU: " + juce::String(engine.cpuUsage() * 100.0, 1) + "%   Latency: "
            + juce::String(engine.outputLatencySamples()) + " samples   Render max: "
            + juce::String(engine.maxRenderUs()) + " us   Overruns: "
            + juce::String(engine.renderOverruns()),
        juce::dontSendNotification);
}

void SettingsDialog::resized() {
    auto area = getLocalBounds().reduced(14);
    title.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);
    auto row = [&](juce::Label& cap, juce::ComboBox& box) {
        auto r = area.removeFromTop(26);
        cap.setBounds(r.removeFromLeft(130));
        box.setBounds(r);
        area.removeFromTop(6);
    };
    row(typeCap, typeBox);
    row(deviceCap, deviceBox);
    row(rateCap, rateBox);
    row(bufferCap, bufferBox);
    row(keyCap, keyBox);
    row(modeCap, modeBox);
    showBpmButton.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    statsLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    auto maint = area.removeFromTop(30);
    clearLibButton.setBounds(maint.removeFromLeft(maint.getWidth() / 2).reduced(2));
    resetMidiButton.setBounds(maint.reduced(2));
    area.removeFromTop(8);
    closeButton.setBounds(area.removeFromTop(30));
}
