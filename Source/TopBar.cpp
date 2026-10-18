// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "TopBar.h"

void TopBar::MasterStrip::push(float v) {
    history[(size_t)pos] = v;
    pos = (pos + 1) % bins;
    repaint();
}

void TopBar::MasterStrip::paint(juce::Graphics& g) {
    const int W = getWidth();
    const int H = getHeight();
    if (W <= 0 || H <= 0)
        return;
    g.fillAll(juce::Colour(0xff0a0d10));
    const float bw = (float)W / bins;
    for (int i = 0; i < bins; ++i) {
        const float v =
            juce::jlimit(0.0f, 1.0f, history[(size_t)((pos + i) % bins)]);
        if (v <= 0.01f)
            continue;
        const float h = v * (float)(H - 4);
        const float x = (float)i * bw;
        g.setColour(DJLook::neonCyan.interpolatedWith(DJLook::neonPink,
                                                      (float)i / bins));
        g.fillRect(x, (float)(H - 2) - h, juce::jmax(1.0f, bw - 1.0f), h);
    }
}

TopBar::TopBar() {
    addAndMakeVisible(wordmark);
    wordmark.setFont(juce::Font(juce::FontOptions{}.withHeight(24.0f)).boldened());
    wordmark.setColour(juce::Label::textColourId, DJLook::ink);

    juce::TextButton* tabs[5] = { &mixTab, &fxTab, &samplerTab, &loopTab, &recTab };
    for (auto* t : tabs) {
        addAndMakeVisible(t);
        t->getProperties().set("tab", 1);
        t->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff9aa4b2));
        t->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        t->setRadioGroupId(400);
    }
    mixTab.setToggleState(true, juce::dontSendNotification);
    recTab.getProperties().set("rec", 1);
    // Only MIX switches a view today; the rest are parked visibly.
    fxTab.setEnabled(false);
    samplerTab.setEnabled(false);
    loopTab.setEnabled(false);
    fxTab.setTooltip("Not in this build");
    samplerTab.setTooltip("Not in this build");
    loopTab.setTooltip("Not in this build");

    addAndMakeVisible(strip);
    addAndMakeVisible(masterKnob);
    addAndMakeVisible(phonesKnob);
    addAndMakeVisible(masterCap);
    addAndMakeVisible(phonesCap);
    for (auto* cap : { &masterCap, &phonesCap }) {
        cap->setJustificationType(juce::Justification::centred);
        cap->setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)).boldened());
    }
    masterKnob.setRange(0.0, 1.25, 0.001);
    masterKnob.setValue(1.0, juce::dontSendNotification);
    phonesKnob.setRange(0.0, 1.25, 0.001);
    phonesKnob.setValue(1.0, juce::dontSendNotification);
    addAndMakeVisible(setupBtn);
    addAndMakeVisible(minBtn);
    addAndMakeVisible(closeBtn);
    addAndMakeVisible(clockLabel);
    for (auto* b : { &setupBtn, &minBtn, &closeBtn }) {
        b->getProperties().set("barbtn", 1);
        b->setColour(juce::TextButton::textColourOffId, DJLook::ink);
        b->setColour(juce::TextButton::textColourOnId, DJLook::ink);
    }
    clockLabel.setColour(juce::Label::textColourId, DJLook::ink);
    clockLabel.setJustificationType(juce::Justification::centredRight);

    minBtn.onClick = [this] {
        if (auto* w = findParentComponentOfClass<juce::ResizableWindow>())
            w->setMinimised(true);
    };
    closeBtn.onClick = [] {
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
    };
}

void TopBar::pushMaster(float peak) {
    strip.push(peak);
}

void TopBar::mouseDown(const juce::MouseEvent& event) {
    dragger.startDraggingComponent(getTopLevelComponent(), event);
}

void TopBar::mouseDrag(const juce::MouseEvent& event) {
    dragger.dragComponent(getTopLevelComponent(), event, nullptr);
}

void TopBar::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat().reduced(6.0f, 6.0f);
    DJLook::drawGlassPanel(g, area, 4.0f);
    DJLook::drawNeonEdge(g, area, 4.0f);

    // Logo badge: neon-ring "6" emblem, mix-cover style, then the wordmark.
    const float ex = 34.0f, ey = 32.0f, er = 19.0f;
    juce::ColourGradient ring(DJLook::neonCyan, ex - er, ey, DJLook::neonPink,
                              ex + er, ey, false);
    g.setGradientFill(ring);
    g.setOpacity(0.9f);
    g.drawEllipse(ex - er, ey - er, er * 2.0f, er * 2.0f, 2.5f);
    g.setOpacity(0.25f);
    g.drawEllipse(ex - er - 3.0f, ey - er - 3.0f, er * 2.0f + 6.0f,
                  er * 2.0f + 6.0f, 2.0f);
    g.setOpacity(1.0f);
    g.setColour(juce::Colour(0xff10141b));
    g.fillEllipse(ex - er + 2.5f, ey - er + 2.5f, er * 2.0f - 5.0f,
                  er * 2.0f - 5.0f);
    g.setColour(DJLook::ink);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(26.0f)).boldened());
    g.drawText("6", juce::roundToInt(ex - er), juce::roundToInt(ey - er),
               juce::roundToInt(er * 2.0f), juce::roundToInt(er * 2.0f),
               juce::Justification::centred);
}

void TopBar::resized() {
    auto area = getLocalBounds().reduced(12, 12);
    // Badge emblem occupies the first 56 px; the wordmark sits beside it.
    area.removeFromLeft(56);
    wordmark.setBounds(area.removeFromLeft(150));
    area.removeFromLeft(8);
    mixTab.setBounds(area.removeFromLeft(80));
    area.removeFromLeft(2);
    fxTab.setBounds(area.removeFromLeft(60));
    area.removeFromLeft(2);
    samplerTab.setBounds(area.removeFromLeft(100));
    area.removeFromLeft(2);
    loopTab.setBounds(area.removeFromLeft(80));
    area.removeFromLeft(2);
    recTab.setBounds(area.removeFromLeft(64));
    area.removeFromLeft(10);
    strip.setBounds(area.removeFromLeft(110));
    area.removeFromLeft(10);
    auto masterCol = area.removeFromLeft(56);
    masterCap.setBounds(masterCol.removeFromTop(14));
    masterKnob.setBounds(masterCol);
    area.removeFromLeft(4);
    auto phonesCol = area.removeFromLeft(56);
    phonesCap.setBounds(phonesCol.removeFromTop(14));
    phonesKnob.setBounds(phonesCol);
    area.removeFromLeft(10);
    closeBtn.setBounds(area.removeFromRight(32));
    area.removeFromRight(4);
    minBtn.setBounds(area.removeFromRight(32));
    area.removeFromRight(8);
    clockLabel.setBounds(area.removeFromRight(56));
    area.removeFromRight(8);
    setupBtn.setBounds(area.removeFromRight(70));
    // Whatever remains between REC and the strip stays empty glass.
}
