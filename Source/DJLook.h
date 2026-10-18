// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// TRANSIT look — the "Modern" theme. A future "Vintage" theme will
// subclass this same LookAndFeel contract; not started, see README.
// Dark console theme: one accent amber, one cool cyan, alarm red.
// Applied as the single LookAndFeel of MainComponent.
class DJLook : public juce::LookAndFeel_V4 {
public:
    DJLook();
    static const juce::Colour bg;       // window background
    static const juce::Colour panel;    // section panels
    static const juce::Colour accent;   // amber: active, filled, playheads of UI
    static const juce::Colour cyan;     // waveforms, secondary highlights
    static const juce::Colour alarm;    // record, clipping
    static const juce::Colour ink;      // primary text
    static const juce::Colour dimInk;   // secondary text
    static const juce::Colour tabBlue;  // active view tab pill
    static const juce::Colour neonCyan; // neon edge / master strip
    static const juce::Colour neonPink; // neon edge / master strip

    // Shared glass-panel language (top bar, browser, future panels).
    static void drawGlassPanel(juce::Graphics& g, juce::Rectangle<float> area,
                               float radius);
    static void drawNeonEdge(juce::Graphics& g, juce::Rectangle<float> area,
                             float radius);

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;
};

// Panel backdrop sharing the exact top-bar glass + neon language.
class GlassPanel : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat().reduced(2.0f);
        DJLook::drawGlassPanel(g, area, 4.0f);
        DJLook::drawNeonEdge(g, area, 4.0f);
    }
};
