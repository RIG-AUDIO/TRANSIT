// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>

#include "DJLook.h"

// Top transport bar: RIG brand badge, wordmark block, view tabs (only MIX
// is a real view today), live master strip, setup, clock, window keys.
// Dragging empty bar space moves the frameless window.
class TopBar : public juce::Component {
public:
    TopBar();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

    // Scrolling master-level history for the mini strip.
    void pushMaster(float peak);

    juce::TextButton mixTab{ "MIX" };
    juce::TextButton fxTab{ "FX" };
    juce::TextButton samplerTab{ "SAMPLER" };
    juce::TextButton loopTab{ "LOOP" };
    juce::TextButton recTab{ "REC" };
    juce::TextButton setupBtn{ "SETUP" };
    juce::TextButton minBtn{ "-" };
    juce::TextButton closeBtn{ "X" };
    juce::Slider masterKnob{ juce::Slider::RotaryHorizontalVerticalDrag,
                             juce::Slider::NoTextBox };
    juce::Slider phonesKnob{ juce::Slider::RotaryHorizontalVerticalDrag,
                             juce::Slider::NoTextBox };
    juce::Label masterCap{ {}, "MASTER" };
    juce::Label phonesCap{ {}, "PHONES" };
    juce::Label clockLabel;
    juce::Label wordmark{ {}, "TRANSIT" };

private:
    class MasterStrip : public juce::Component {
    public:
        void push(float v);
        void paint(juce::Graphics& g) override;

    private:
        static constexpr int bins = 96;
        std::array<float, bins> history = {};
        int pos = 0;
    };

    juce::ComponentDragger dragger;
    MasterStrip strip;
    std::unique_ptr<juce::Drawable> rigMark; // embedded SVG (BinaryData)
};
