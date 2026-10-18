// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Dev tool (not a gate): renders MainComponent offscreen to a PNG so UI
// changes can be reviewed without a live window. Usage:
//   UiShot.exe <output.png>
//
// NOTE: headless JUCE painting proved unreliable on this box (paint calls
// silently write nothing without a live peer/message pump), so this tool is
// kept minimal: if the snapshot comes out empty, use a live screenshot.
#include <juce_gui_extra/juce_gui_extra.h>

#include "../Source/MainComponent.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: UiShot <output.png>\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    MainComponent content;
    content.setSize(1100, 824);
    content.setVisible(true);
    content.resized();
    if (argc > 2 && juce::String(argv[2]) == "--layout") {
        content.dumpLayout();
        return 0;
    }
    juce::Image shot = content.createComponentSnapshot(
        content.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    juce::File out(argv[1]);
    juce::FileOutputStream stream(out);
    if (!stream.openedOk() || !png.writeImageToStream(shot, stream)) {
        std::printf("UiShot: write failed\n");
        return 1;
    }
    stream.flush();
    std::printf("UiShot: wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
