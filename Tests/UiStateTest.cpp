// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// W8 gate: the zoom-state persistence rules (parse/serialize) are JUCE-free
// so they can be tested without a message loop or a real settings file.
#include "UiState.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool cond, const char* name) {
    std::printf("UISTATE_TEST %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        ++failures;
}

} // namespace

int main() {
    using lean::UiState;

    const UiState defaults;
    check(defaults.zoom[0] == 3 && defaults.zoom[1] == 3, "defaults 3/3");

    const UiState parsed =
        lean::parseUiState("mode=free\nzoom0=1\nzoom1=6\nshowBpm=1\n");
    check(parsed.zoom[0] == 1 && parsed.zoom[1] == 6, "parses both zooms");

    const UiState empty = lean::parseUiState("");
    check(empty.zoom[0] == 3 && empty.zoom[1] == 3, "empty keeps defaults");

    const UiState partial = lean::parseUiState("zoom0=0\n");
    check(partial.zoom[0] == 0 && partial.zoom[1] == 3,
          "partial keeps the other default");

    const UiState clamped = lean::parseUiState("zoom0=99\nzoom1=-3\n");
    check(clamped.zoom[0] == 7 && clamped.zoom[1] == 0,
          "out-of-range clamps to the table");

    const UiState garbage = lean::parseUiState("zoom0=abc\nzoom1=\n");
    check(garbage.zoom[0] == 3 && garbage.zoom[1] == 3,
          "malformed values keep the defaults");

    const UiState round = lean::parseUiState(lean::serializeUiState(parsed));
    check(round.zoom[0] == 1 && round.zoom[1] == 6, "serialize/parse round trip");

    const UiState spaces = lean::parseUiState("zoom0= 5 \n");
    check(spaces.zoom[0] == 5, "tolerates surrounding spaces");

    // W9: browser scroll position, stored in permille.
    check(lean::parseUiState("scroll=42\n").scroll == 42, "parses scroll");
    check(lean::parseUiState("scroll=-5\n").scroll == 0,
          "negative scroll keeps the default");
    check(lean::parseUiState("scroll=abc\n").scroll == 0,
          "malformed scroll keeps the default");
    check(lean::parseUiState("scroll=5000\n").scroll == 1000,
          "scroll clamps to 1000 permille");

    // W9: per-file zoom memory.
    const UiState files = lean::parseUiState(
        "filezoom=/a/b.wav|4\nfilezoom=/a=c/d e.wav|2\n");
    check(files.fileZoom.size() == 2, "parses two filezoom lines");
    const int* zb = lean::findFileZoom(files, "/a/b.wav");
    check(zb != nullptr && *zb == 4, "finds the stored file zoom");
    const int* zd = lean::findFileZoom(files, "/a=c/d e.wav");
    check(zd != nullptr && *zd == 2, "path with '=' and spaces stays intact");
    check(lean::findFileZoom(files, "/a/nope.wav") == nullptr,
          "unknown file has no memory");

    const UiState bad = lean::parseUiState(
        "filezoom=nopipe\nfilezoom=|4\nfilezoom=/x|\nfilezoom=/y|9\n"
        "filezoom=/z|-2\n");
    check(bad.fileZoom.size() == 2, "malformed filezoom lines are dropped");
    const int* y = lean::findFileZoom(bad, "/y");
    check(y != nullptr && *y == 7, "stored zoom clamps to the table (high)");
    const int* z = lean::findFileZoom(bad, "/z");
    check(z != nullptr && *z == 0, "stored zoom clamps to the table (low)");

    const UiState dedup = lean::parseUiState(
        "filezoom=/same.wav|1\nfilezoom=/same.wav|6\n");
    check(dedup.fileZoom.size() == 1, "a repeated path keeps one entry");
    const int* same = lean::findFileZoom(dedup, "/same.wav");
    check(same != nullptr && *same == 6, "the newest record wins");

    std::string many;
    for (int i = 0; i < 70; ++i)
        many += "filezoom=/f" + std::to_string(i) + "|1\n";
    const UiState capped = lean::parseUiState(many);
    check(capped.fileZoom.size() == lean::kMaxFileZoomEntries,
          "file memory is capped");
    check(capped.fileZoom.front().first == "/f6" && !lean::findFileZoom(capped, "/f5"),
          "the oldest entries are dropped first");

    const UiState round2 = lean::parseUiState(lean::serializeUiState(files));
    check(round2.scroll == 0 && round2.fileZoom.size() == 2
              && lean::findFileZoom(round2, "/a=c/d e.wav") != nullptr,
          "filezoom serialize/parse round trip");

    const UiState round3 = lean::parseUiState(
        lean::serializeUiState(lean::UiState{ { 1, 6 }, 17, { { "/p q.wav", 3 } } }));
    check(round3.zoom[0] == 1 && round3.zoom[1] == 6 && round3.scroll == 17,
          "full state round trip (zooms + scroll)");
    const int* pq = lean::findFileZoom(round3, "/p q.wav");
    check(pq != nullptr && *pq == 3, "full state round trip (file zoom)");

    // Round 5: MIDI device name key.
    check(defaults.midi.empty(), "default midi empty");
    check(lean::parseUiState("midi=DDJ-FLX4 MIDI 1\n").midi
              == "DDJ-FLX4 MIDI 1",
          "parses midi name");
    check(lean::parseUiState("mode=sync\nmidi=DEV\n").midi == "DEV",
          "midi coexists with other keys");
    check(lean::parseUiState("midi=\n").midi.empty(), "empty midi stays empty");
    check(lean::parseUiState("midi=DEV\r\n").midi == "DEV",
          "strips trailing CR");
    check(lean::parseUiState("midi=DEV \n").midi == "DEV",
          "strips trailing space");
    const UiState midiState = lean::parseUiState("midi=DDJ-FLX4\n");
    const UiState midiRound = lean::parseUiState(lean::serializeUiState(midiState));
    check(midiRound.midi == "DDJ-FLX4", "midi serialize/parse round trip");
    check(lean::serializeUiState(defaults).find("midi=") == std::string::npos,
          "empty midi is not serialized");

    if (failures == 0)
        std::printf("UISTATE_TEST PASS\n");
    else
        std::printf("UISTATE_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
