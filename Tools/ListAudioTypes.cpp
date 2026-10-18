// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Dev probe (not a gate): lists audio device types and outputs, proving
// which drivers this build can see — including ASIO. Run manually.
#include <juce_audio_devices/juce_audio_devices.h>

#include <cstdio>

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioDeviceManager devices;
    for (auto* type : devices.getAvailableDeviceTypes()) {
        std::printf("TYPE %s\n", type->getTypeName().toRawUTF8());
        for (auto& name : type->getDeviceNames(false))
            std::printf("  OUT %s\n", name.toRawUTF8());
    }
    return 0;
}
