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

    if (failures == 0)
        std::printf("UISTATE_TEST PASS\n");
    else
        std::printf("UISTATE_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
