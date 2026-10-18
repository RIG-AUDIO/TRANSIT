// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Dev probe (not a gate): end-to-end VST3 chain on this machine — scan,
// load the first bundle found, prepare, process one block, unload.
// Usage: FxProbe.exe
#include <juce_gui_extra/juce_gui_extra.h>

#include "../Source/FxRack.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    FxRack rack;
    const auto found = rack.scanVst3();
    std::printf("FxProbe: %d bundles found\n", found.size());
    if (found.isEmpty())
        return 0;
    // Try bundles in order; 32-bit ones in (x86) correctly refuse to load.
    juce::String loadedPath;
    for (const auto& entry : found) {
        const auto path = entry.fromFirstOccurrenceOf("|", false, false);
        juce::String error;
        std::printf("FxProbe: trying %s\n", entry.toRawUTF8());
        if (rack.loadPlugin(0, path, error)) {
            loadedPath = path;
            break;
        }
        std::printf("FxProbe: skip: %s\n", error.toRawUTF8());
    }
    if (loadedPath.isEmpty()) {
        std::printf("FxProbe: nothing loadable\n");
        return 1;
    }
    std::printf("FxProbe: loaded '%s'\n", rack.slotName(0).toRawUTF8());
    rack.prepare(48000.0, 1024);

    constexpr int n = 512;
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i) {
        L[(size_t)i] =
            0.5f * (float)sin(2.0 * 3.141592653589793 * 440.0 * i / 48000.0);
        R[(size_t)i] = L[(size_t)i];
    }
    rack.processAux(0, L.data(), R.data(), n);
    double peak = 0.0;
    bool finite = true;
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i]))
            finite = false;
        peak = std::max(peak, (double)std::abs(L[(size_t)i]));
    }
    std::printf("FxProbe: processed, finite=%d peak=%.4f (dry would be 0.5)\n",
                (int)finite, peak);
    rack.unloadPlugin(0);
    rack.pumpRetired();
    std::printf("FxProbe: PASS\n");
    return finite ? 0 : 1;
}
