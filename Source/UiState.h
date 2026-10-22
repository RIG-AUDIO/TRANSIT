// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Cross-session UI state (W8/W9): the per-deck waveform zoom index, the
// browser's top row, and the per-file zoom memory.
//
// Kept JUCE-free and header-only so the parse/serialize rules can be gated
// by a plain unit test (Tests/UiStateTest.cpp) without a message loop;
// MainComponent embeds the result in its settings file.
#pragma once

#include "Waveform.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lean {

// The per-file zoom memory is bounded so the settings file cannot grow with
// the library: entries are most-recent-first from the back, oldest dropped.
inline constexpr size_t kMaxFileZoomEntries = 64;

struct UiState {
    // Zoom index per deck (lean::kZoomSeconds; 3 = 32 s, 7 = whole track).
    int zoom[2] = { 3, 3 };
    // Browser list position in permille of the scrollbar range (0..1000),
    // clamped when parsed; applied as ListBox::setVerticalPosition.
    int scroll = 0;
    // Per-file waveform zoom memory: path -> zoom index, MRU at the back.
    std::vector<std::pair<std::string, int>> fileZoom;
    // Last-selected MIDI input device name ("" = none). Restored at startup
    // by matching against the enumerated inputs.
    std::string midi;
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

// Records path -> zoom as the most-recent entry: any previous record for the
// same path is dropped, the new one lands at the back, and the oldest entries
// beyond the cap go. Empty paths are refused; the zoom is clamped to the
// table so a stored file can never hold a value the view rejects.
inline void putFileZoom(UiState& state, std::string path, int zoom) {
    if (path.empty())
        return;
    const int clamped = clampZoomIndex(zoom);
    auto& v = state.fileZoom;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].first == path) {
            v.erase(v.begin() + (std::ptrdiff_t)i);
            break;
        }
    }
    v.emplace_back(std::move(path), clamped);
    if (v.size() > kMaxFileZoomEntries)
        v.erase(v.begin(), v.end() - (std::ptrdiff_t)kMaxFileZoomEntries);
}

// Most-recent lookup for a load request; nullptr means "no memory for this
// file" (the deck keeps whatever zoom it currently shows).
inline const int* findFileZoom(const UiState& state, std::string_view path) {
    for (auto it = state.fileZoom.rbegin(); it != state.fileZoom.rend(); ++it)
        if (it->first == path)
            return &it->second;
    return nullptr;
}

// "path|zoom" split on the LAST '|' so paths containing '=' or spaces stay
// intact; the zoom must parse as an integer or the line is dropped.
inline void parseFileZoomLine(std::string_view rhs, UiState& state) {
    const size_t sep = rhs.rfind('|');
    if (sep == std::string_view::npos)
        return;
    const std::string_view path = rhs.substr(0, sep);
    if (path.empty())
        return;
    int zoom = 0;
    if (!parseUiInt(rhs.substr(sep + 1), zoom))
        return;
    putFileZoom(state, std::string(path), clampZoomIndex(zoom));
}

// Reads "key=value" lines; zoom0/zoom1/scroll/filezoom are consumed here,
// every other line is ignored (the settings file is shared with the rest of
// the UI state). Missing or malformed values keep the defaults; numeric
// values outside the zoom table are clamped; a negative scroll keeps 0 and
// a scroll above 1000 clamps to 1000 permille.
inline UiState parseUiState(std::string_view text) {
    UiState state;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t eol = text.find('\n', pos);
        const std::string_view line = text.substr(
            pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
        const size_t eq = line.find('=');
        if (eq != std::string_view::npos) {
            const std::string_view key = line.substr(0, eq);
            const std::string_view rhs = line.substr(eq + 1);
            if (key == "filezoom") {
                parseFileZoomLine(rhs, state);
            } else if (key == "midi") {
                std::string name(rhs);
                while (!name.empty()
                       && (name.back() == '\r' || name.back() == ' '))
                    name.pop_back();
                state.midi = std::move(name);
            } else {
                int value = 0;
                if (parseUiInt(rhs, value)) {
                    if (key == "zoom0")
                        state.zoom[0] = clampZoomIndex(value);
                    else if (key == "zoom1")
                        state.zoom[1] = clampZoomIndex(value);
                    else if (key == "scroll" && value >= 0)
                        state.scroll = value > 1000 ? 1000 : value;
                }
            }
        }
        if (eol == std::string_view::npos)
            break;
        pos = eol + 1;
    }
    return state;
}

// Serializes the zoom indices, the scroll row and the per-file zoom memory
// as settings-file lines (file zooms in most-recent-last order, matching
// the parse side's cap behaviour).
inline std::string serializeUiState(const UiState& state) {
    std::string out;
    out += "zoom0=" + std::to_string(state.zoom[0]) + "\n";
    out += "zoom1=" + std::to_string(state.zoom[1]) + "\n";
    out += "scroll=" + std::to_string(state.scroll) + "\n";
    for (const auto& entry : state.fileZoom)
        out += "filezoom=" + entry.first + "|" + std::to_string(entry.second)
            + "\n";
    if (!state.midi.empty())
        out += "midi=" + state.midi + "\n";
    return out;
}

} // namespace lean
