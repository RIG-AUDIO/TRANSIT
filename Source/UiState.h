// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Cross-session UI state (W8): the per-deck waveform zoom index.
//
// Kept JUCE-free and header-only so the parse/serialize rules can be gated
// by a plain unit test (Tests/UiStateTest.cpp) without a message loop;
// MainComponent embeds the result in its settings file.
#pragma once

#include "Waveform.h"

#include <string>
#include <string_view>

namespace lean {

struct UiState {
    // Zoom index per deck (lean::kZoomSeconds; 3 = 32 s, 7 = whole track).
    int zoom[2] = { 3, 3 };
};

// Strict integer parse: no digits means "no value" (the caller keeps its
// default); a leading sign is accepted so out-of-range input can be clamped.
inline bool parseUiInt(std::string_view text, int& out) {
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }
    if (i >= text.size() || text[i] < '0' || text[i] > '9')
        return false;
    long value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (text[i] - '0');
        ++i;
    }
    out = (int)(negative ? -value : value);
    return true;
}

// Reads "key=value" lines; only zoom0/zoom1 are consumed here, every other
// line is ignored (the settings file is shared with the rest of the UI
// state). Missing or malformed values keep the defaults; numeric values
// outside the table are clamped to a valid zoom index.
inline UiState parseUiState(std::string_view text) {
    UiState state;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t eol = text.find('\n', pos);
        const std::string_view line = text.substr(
            pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
        const size_t eq = line.find('=');
        if (eq != std::string_view::npos) {
            int value = 0;
            const std::string_view key = line.substr(0, eq);
            if (parseUiInt(line.substr(eq + 1), value)) {
                if (key == "zoom0")
                    state.zoom[0] = clampZoomIndex(value);
                else if (key == "zoom1")
                    state.zoom[1] = clampZoomIndex(value);
            }
        }
        if (eol == std::string_view::npos)
            break;
        pos = eol + 1;
    }
    return state;
}

// Serializes the zoom indices as settings-file lines.
inline std::string serializeUiState(const UiState& state) {
    std::string out;
    out += "zoom0=" + std::to_string(state.zoom[0]) + "\n";
    out += "zoom1=" + std::to_string(state.zoom[1]) + "\n";
    return out;
}

} // namespace lean
