// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

#include "AudioEngine.h"

// Modal setup popup: audio device chain, keylock quality, library/MIDI
// maintenance. Device changes apply live through AudioEngine.
class SettingsDialog : public juce::Component, private juce::Timer {
public:
    SettingsDialog(AudioEngine& e, bool showBpmInit,
                   std::function<void()> onLibraryCleared,
                   std::function<void()> onMidiReset,
                   std::function<void(bool syncMode)> onModeChanged,
                   std::function<void(bool showBpm)> onShowBpm,
                   std::function<void()> onPersist);
    void resized() override;

private:
    void timerCallback() override;
    void refreshDevices();
    void refreshRatesAndBuffers();
    void fillStrings(juce::ComboBox& box, const juce::StringArray& items,
                     const juce::String& current);
    void fillInts(juce::ComboBox& box, const juce::Array<int>& items, int current);

    AudioEngine& engine;
    std::function<void()> onLibraryCleared;
    std::function<void()> onMidiReset;
    std::function<void(bool)> onModeChanged;
    std::function<void(bool)> onShowBpm;
    std::function<void()> onPersist;

    juce::Label title{ {}, "TRANSIT - Setup" };
    juce::Label typeCap{ {}, "Driver" };
    juce::ComboBox typeBox;
    juce::Label deviceCap{ {}, "Output device" };
    juce::ComboBox deviceBox;
    juce::Label rateCap{ {}, "Sample rate" };
    juce::ComboBox rateBox;
    juce::Label bufferCap{ {}, "Buffer size" };
    juce::ComboBox bufferBox;
    juce::Label keyCap{ {}, "Keylock quality" };
    juce::ComboBox keyBox;
    juce::Label modeCap{ {}, "Beatmatching" };
    juce::ComboBox modeBox;
    juce::ToggleButton showBpmButton{ "Show track BPM" };
    juce::Label statsLabel;
    juce::TextButton clearLibButton{ "Clear library index" };
    juce::TextButton resetMidiButton{ "Reset MIDI map" };
    juce::TextButton closeButton{ "Close" };
};
