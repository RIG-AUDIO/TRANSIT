// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "DJLook.h"

#include <cmath>

#include <BinaryData.h>

const juce::Colour DJLook::bg(0xff1b1f26);
const juce::Colour DJLook::panel(0xff232833);
const juce::Colour DJLook::deep(0xff0e1116);
const juce::Colour DJLook::well(0xff191e27);
const juce::Colour DJLook::accent(0xffffb454);
const juce::Colour DJLook::cyan(0xff5fd4ff);
const juce::Colour DJLook::alarm(0xffff5252);
const juce::Colour DJLook::ink(0xffe9eef6);
const juce::Colour DJLook::dimInk(0xff8f9bad);
const juce::Colour DJLook::focus(0xff1a9fff);

float DJLook::meterUnit(float amplitude) noexcept {
    if (!(amplitude > 1.0e-6f)) // <= 0 and NaN land at the floor
        return 0.0f;
    const float db = 20.0f * std::log10(amplitude);
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

void DJLook::drawGlassPanel(juce::Graphics& g, juce::Rectangle<float> area,
                            float radius) {
    juce::ColourGradient body(juce::Colour(0xff1f2530), area.getX(), area.getY(),
                              juce::Colour(0xff0f1217), area.getX(),
                              area.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(area, radius);
    // Top glass highlight.
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawLine(area.getX() + 12.0f, area.getY() + 1.0f, area.getRight() - 12.0f,
               area.getY() + 1.0f, 1.0f);
}

void DJLook::drawLitBorder(juce::Graphics& g, juce::Rectangle<float> area,
                           float radius) {
    // Thin lit hairline: structure without neon glow (review G2).
    g.setColour(ink.withAlpha(0.20f));
    g.drawRoundedRectangle(area, radius, 1.0f);
}

namespace {

juce::Typeface::Ptr loadFace(const void* data, size_t size) {
    return juce::Typeface::createSystemTypefaceFor(data, size);
}

} // namespace

juce::Typeface::Ptr DJLook::getTypefaceForFont(const juce::Font& font) {
    // Adriatica Next, embedded under OFL (see Source/Assets/OFL.txt).
    // Bold requests get Condensed Bold, everything else gets Medium.
    static const juce::Typeface::Ptr medium = loadFace(
        TransitFonts::AdriaticaNextMedium_ttf,
        TransitFonts::AdriaticaNextMedium_ttfSize);
    static const juce::Typeface::Ptr condensed = loadFace(
        TransitFonts::AdriaticaNextCondensedBold_ttf,
        TransitFonts::AdriaticaNextCondensedBold_ttfSize);
    return font.isBold() ? condensed : medium;
}

DJLook::DJLook() {
    setColour(juce::Label::textColourId, ink);
    setColour(juce::Label::textWhenEditingColourId, ink);
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(0.4f));
    setColour(juce::TextEditor::backgroundColourId, panel);
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.4f));
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, focus);
    setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff141821));
    setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, panel);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::buttonColourId, dimInk);
    setColour(juce::ComboBox::focusedOutlineColourId, focus);
    setColour(juce::TextButton::textColourOnId, bg);
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::ScrollBar::thumbColourId, dimInk.withAlpha(0.6f));
    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
}

void DJLook::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float /*min*/, float /*max*/,
                              const juce::Slider::SliderStyle style,
                              juce::Slider& slider) {
    const bool vertical = style == juce::Slider::LinearVertical;
    if (vertical) {
        const int cx = x + width / 2;
        // Recessed well under the groove.
        g.setColour(deep);
        g.fillRoundedRectangle((float)cx - 3.0f, (float)y, 6.0f, (float)height, 3.0f);
        g.setColour(ink.withAlpha(0.10f));
        g.drawRoundedRectangle((float)cx - 3.0f, (float)y, 6.0f, (float)height, 3.0f,
                               1.0f);
        g.setColour(accent);
        const float top = juce::jmin(sliderPos, (float)(y + height));
        g.fillRoundedRectangle((float)cx - 2.0f, top, 4.0f,
                               (float)(y + height) - top, 2.0f);
        // Dark hardware cap with a lit rim and an amber indicator line.
        const float capY = sliderPos - 6.0f;
        g.setColour(panel.brighter(0.18f));
        g.fillRoundedRectangle((float)cx - 17.0f, capY, 34.0f, 12.0f, 3.0f);
        g.setColour(ink.withAlpha(0.25f));
        g.drawRoundedRectangle((float)cx - 17.0f, capY, 34.0f, 12.0f, 3.0f, 1.0f);
        g.setColour(accent);
        g.fillRoundedRectangle((float)cx - 11.0f, sliderPos - 1.0f, 22.0f, 2.0f,
                               1.0f);
    } else {
        const int cy = y + height / 2;
        g.setColour(deep);
        g.fillRoundedRectangle((float)x, (float)cy - 3.0f, (float)width, 6.0f, 3.0f);
        g.setColour(ink.withAlpha(0.10f));
        g.drawRoundedRectangle((float)x, (float)cy - 3.0f, (float)width, 6.0f, 3.0f,
                               1.0f);
        g.setColour(accent);
        g.fillRoundedRectangle((float)x, (float)cy - 2.0f, sliderPos - (float)x, 4.0f,
                               2.0f);
        // Round cap, dark with a lit rim and amber centre.
        g.setColour(panel.brighter(0.18f));
        g.fillEllipse(sliderPos - 7.0f, (float)cy - 7.0f, 14.0f, 14.0f);
        g.setColour(ink.withAlpha(0.25f));
        g.drawEllipse(sliderPos - 7.0f, (float)cy - 7.0f, 14.0f, 14.0f, 1.0f);
        g.setColour(accent);
        g.fillEllipse(sliderPos - 3.5f, (float)cy - 3.5f, 7.0f, 7.0f);
    }
    if (slider.hasKeyboardFocus(true)) {
        g.setColour(focus);
        g.drawRoundedRectangle(
            juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height)
                .reduced(1.0f),
            4.0f, 1.5f);
    }
}

void DJLook::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) {
    const float cx = (float)x + (float)width * 0.5f;
    const float cy = (float)y + (float)height * 0.5f;
    const float radius = (float)juce::jmin(width, height) * 0.38f;
    juce::Path track;
    track.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle,
                        true);
    g.setColour(ink.withAlpha(0.14f));
    g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    const float angle =
        rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    juce::Path fill;
    float pointerAngle = angle;
    if (slider.getProperties().contains("bipolar")) {
        // Center-detent EQ knob: the fill grows outward from 12 o'clock, so
        // flat (0 dB) always renders centered however the range is skewed.
        // The pointer is remapped the same way: value 0 sits at 12 o'clock.
        const float center = (rotaryStartAngle + rotaryEndAngle) * 0.5f;
        const auto range = slider.getRange();
        const double span = range.getLength() > 0.0 ? range.getLength() : 1.0;
        const double centerProp =
            juce::jlimit(0.0, 1.0, (0.0 - range.getStart()) / span);
        const float half = (rotaryEndAngle - rotaryStartAngle) * 0.5f;
        if (sliderPosProportional >= (float)centerProp)
            pointerAngle = center
                + (float)((sliderPosProportional - centerProp)
                          / (1.0 - centerProp + 1e-9) * half);
        else
            pointerAngle = center
                - (float)((centerProp - sliderPosProportional)
                          / (centerProp + 1e-9) * half);
        // Fill to the remapped pointer angle, not the raw one: with a skewed
        // range (e.g. -24..+6 dB) the raw angle sits well past the detent at
        // 0 dB and would paint a phantom boost arc at rest.
        if (pointerAngle >= center)
            fill.addCentredArc(cx, cy, radius, radius, 0.0f, center, pointerAngle,
                               true);
        else
            fill.addCentredArc(cx, cy, radius, radius, 0.0f, pointerAngle, center,
                               true);
    } else {
        fill.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle,
                           true);
    }
    g.setColour(accent);
    g.strokePath(fill, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    // Dark console body with a thin lit rim (no white plastic disc).
    const float bodyR = radius * 0.58f;
    g.setColour(panel.brighter(0.12f));
    g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour(ink.withAlpha(0.20f));
    g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);
    juce::Path pointer;
    const float pa = pointerAngle - juce::MathConstants<float>::halfPi;
    const float sx = cx + std::cos(pa) * radius * 0.12f;
    const float sy = cy + std::sin(pa) * radius * 0.12f;
    const float px = cx + std::cos(pa) * radius * 0.50f;
    const float py = cy + std::sin(pa) * radius * 0.50f;
    pointer.addLineSegment(juce::Line<float>(sx, sy, px, py), 2.5f);
    g.setColour(accent);
    g.fillPath(pointer);
}

void DJLook::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                  const juce::Colour& /*backgroundColour*/,
                                  bool highlighted, bool down) {
    const auto& props = button.getProperties();
    // View tabs (top bar): transparent at rest, flat info-cyan pill when
    // active, alarm-red pill for REC while recording (review G2: no glow).
    if (props.contains("tab")) {
        const bool on = button.getToggleState();
        auto area = button.getLocalBounds().toFloat().reduced(2.0f, 5.0f);
        const bool isRec = props.contains("rec");
        if (on) {
            g.setColour(isRec ? alarm : cyan);
            g.fillRoundedRectangle(area, 5.0f);
        } else if (highlighted && button.isEnabled()) {
            g.setColour(juce::Colour(0xff1e2432));
            g.fillRoundedRectangle(area, 5.0f);
        }
        return;
    }
    // Bar buttons (min/close/setup): transparent, panel on hover.
    if (props.contains("barbtn")) {
        auto area = button.getLocalBounds().toFloat().reduced(1.0f);
        if (down || highlighted) {
            g.setColour(panel.brighter(0.3f));
            g.fillRoundedRectangle(area, 4.0f);
        }
        return;
    }
    // Chunky hardware keys, Xone-style: near-black at rest with a dim amber
    // bezel, full amber backlight when engaged, no aero gloss anywhere.
    const bool on = button.getToggleState();
    auto area = button.getLocalBounds().toFloat().reduced(1.0f);
    if (on) {
        g.setColour(accent.withAlpha(0.25f));
        g.fillRoundedRectangle(area.expanded(2.0f), 5.0f);
        g.setColour(down ? accent.darker(0.15f) : accent);
        g.fillRoundedRectangle(area, 4.0f);
    } else {
        g.setColour(deep);
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour((highlighted || down) ? accent : accent.withAlpha(0.35f));
        g.drawRoundedRectangle(area, 4.0f, highlighted ? 1.5f : 1.0f);
    }
    if (button.hasKeyboardFocus(true)) {
        g.setColour(focus);
        g.drawRoundedRectangle(area.expanded(1.5f), 6.0f, 1.5f);
    }
}

void DJLook::drawComboBox(juce::Graphics& g, int width, int height, bool /*down*/,
                          int buttonX, int buttonY, int buttonW, int buttonH,
                          juce::ComboBox& box) {
    auto area = juce::Rectangle<int>(width, height).toFloat().reduced(1.0f);
    g.setColour(panel.brighter(0.08f));
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour(box.hasKeyboardFocus(true) ? accent : dimInk.withAlpha(0.5f));
    g.drawRoundedRectangle(area, 5.0f, 1.0f);
    juce::Path arrow;
    const float cx = (float)buttonX + (float)buttonW * 0.5f;
    const float cy = (float)buttonY + (float)buttonH * 0.5f;
    arrow.addTriangle(cx - 4.0f, cy - 2.5f, cx + 4.0f, cy - 2.5f, cx, cy + 3.0f);
    g.setColour(dimInk);
    g.fillPath(arrow);
}
