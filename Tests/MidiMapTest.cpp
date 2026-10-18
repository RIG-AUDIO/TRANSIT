// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// MIDI map gates: bind/lookup, unknown CC misses, save/load roundtrip,
// invalid text rejected, clear removes.
#include "MidiMap.h"
#include "MidiQueue.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool cond, const char* name) {
    std::printf("MIDIMAP_TEST %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        ++failures;
}

int run() {
    lean::MidiMap map;
    check(!map.lookup(1, 16).has_value(), "empty miss");
    map.bind(1, 16, 0, lean::MidiTarget::Fader);
    map.bind(1, 17, 1, lean::MidiTarget::Tempo);
    map.bind(2, 7, 0, lean::MidiTarget::Xfade);
    check(map.size() == 3, "size 3");
    auto b = map.lookup(1, 16);
    check(b.has_value() && b->deck == 0 && b->target == lean::MidiTarget::Fader,
          "lookup fader");
    check(!map.lookup(1, 18).has_value(), "wrong cc miss");
    check(!map.lookup(3, 16).has_value(), "wrong channel miss");

    // Rebind replaces.
    map.bind(1, 16, 1, lean::MidiTarget::Gain);
    check(map.size() == 3, "rebind same size");
    auto b2 = map.lookup(1, 16);
    check(b2.has_value() && b2->deck == 1 && b2->target == lean::MidiTarget::Gain,
          "rebind replaces");

    // Persist roundtrip.
    const std::string text = map.save();
    lean::MidiMap map2;
    check(map2.load(text), "load ok");
    check(map2.size() == 3, "roundtrip size");
    auto b3 = map2.lookup(2, 7);
    check(b3.has_value() && b3->target == lean::MidiTarget::Xfade, "roundtrip xfade");

    // Invalid text rejected, map untouched.
    lean::MidiMap map3;
    map3.bind(1, 1, 0, lean::MidiTarget::Master);
    check(!map3.load("bogus line here\n"), "bad line rejected");
    check(map3.size() == 1, "rejected load keeps map");
    check(!map3.load("1 200 0 1\n"), "bad cc rejected");

    map2.clear(1, 16);
    check(!map2.lookup(1, 16).has_value() && map2.size() == 2, "clear removes");

    // Realtime grid: bind/lookup pack deck+target, misses read -1.
    lean::MidiGrid grid;
    check(grid.lookup(1, 16) < 0, "grid empty miss");
    grid.bind(1, 16, 1, lean::MidiTarget::Fader);
    grid.bind(16, 127, 0, lean::MidiTarget::Xfade);
    const int code = grid.lookup(1, 16);
    check(code >= 0 && lean::MidiGrid::gridDeck(code) == 1
              && lean::MidiGrid::gridTarget(code) == lean::MidiTarget::Fader,
          "grid pack");
    check(lean::MidiGrid::gridTarget(grid.lookup(16, 127)) == lean::MidiTarget::Xfade,
          "grid global");
    grid.clear(1, 16);
    check(grid.lookup(1, 16) < 0, "grid clear");
    check(grid.lookup(0, 16) < 0 && grid.lookup(1, 200) < 0, "grid bounds");

    // CC-to-value mapping hits each target's native range.
    check(std::abs(lean::midiValue(lean::MidiTarget::Tempo, 0.0f) - 0.5f) < 1e-6f, "tempo min");
    check(std::abs(lean::midiValue(lean::MidiTarget::Tempo, 1.0f) - 1.5f) < 1e-6f, "tempo max");
    check(std::abs(lean::midiValue(lean::MidiTarget::Low, 0.0f) - -24.0f) < 1e-6f, "eq min");
    check(std::abs(lean::midiValue(lean::MidiTarget::Low, 1.0f) - 6.0f) < 1e-6f, "eq max");
    check(std::abs(lean::midiValue(lean::MidiTarget::Fader, 0.5f) - 0.5f) < 1e-6f, "fader mid");
    check(std::abs(lean::midiValue(lean::MidiTarget::Master, 1.0f) - 1.25f) < 1e-6f, "master max");

    // Timestamped queue: order preserved, drain respects the frame bound.
    lean::MidiQueue queue;
    queue.push(100, 1, 16, 0.5f);
    queue.push(200, 1, 17, 0.6f);
    queue.push(300, 1, 18, 0.7f);
    lean::MidiEvent out[8];
    check(queue.drain(150, out, 8) == 1 && out[0].cc == 16, "drain bound");
    check(queue.drain(1000, out, 8) == 2 && out[0].cc == 17 && out[1].cc == 18,
          "drain rest");
    check(queue.drain(1000, out, 8) == 0, "drain empty");
    check(queue.droppedCount() == 0, "no drops");

    if (failures == 0)
        std::printf("MIDIMAP_TEST PASS\n");
    else
        std::printf("MIDIMAP_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
