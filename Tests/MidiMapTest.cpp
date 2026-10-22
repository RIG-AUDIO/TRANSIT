// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// MIDI map gates: bind/lookup, unknown CC misses, save/load roundtrip,
// invalid text rejected, clear removes; DDJ-FLX4 factory-map facts
// (value/action tables, 14-bit fold, relative decode, name match,
// soft-takeover pickup, action mailbox) from Source/Flx4Map.h.
#include "Flx4Map.h"
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

    // midiValue: Send is a 0..1 passthrough (new target, appended last).
    check(std::abs(lean::midiValue(lean::MidiTarget::Send, 0.7f) - 0.7f)
              < 1e-6f,
          "send passthrough");

    // --- DDJ-FLX4 value table (hardware facts) ---
    int nv = 0;
    const lean::Flx4ValueBind* vt = lean::flx4ValueTable(nv);
    check(nv == 16, "flx4 value count");
    const lean::Flx4ValueBind* tb = lean::flx4FindValue(1, 0x00);
    check(tb != nullptr && tb->target == lean::MidiTarget::Tempo
              && tb->deck == 0,
          "flx4 deck1 tempo");
    check(lean::flx4FindValue(2, 0x00) != nullptr, "flx4 deck2 tempo");
    check(lean::flx4FindValue(1, 0x13) != nullptr
              && lean::flx4FindValue(1, 0x13)->target == lean::MidiTarget::Fader,
          "flx4 deck1 fader");
    const lean::Flx4ValueBind* xf = lean::flx4FindValue(7, 0x1F);
    check(xf != nullptr && xf->target == lean::MidiTarget::Xfade
              && xf->deck == 0,
          "flx4 crossfader");
    const lean::Flx4ValueBind* s0 = lean::flx4FindValue(7, 0x17);
    const lean::Flx4ValueBind* s1 = lean::flx4FindValue(7, 0x18);
    check(s0 != nullptr && s0->target == lean::MidiTarget::Send
              && s0->deck == 0,
          "flx4 cfx1 -> send deck0");
    check(s1 != nullptr && s1->target == lean::MidiTarget::Send
              && s1->deck == 1,
          "flx4 cfx2 -> send deck1");
    check(lean::flx4FindValue(7, 0x0C) != nullptr, "flx4 blend");
    check(lean::flx4FindValue(1, 0x40) == nullptr, "flx4 value miss");
    // Every LSB partner must fit the 0..127 range (msb + 0x20).
    bool lsbok = true;
    for (int i = 0; i < nv; ++i)
        if (vt[i].msb + 0x20 > 127)
            lsbok = false;
    check(lsbok, "flx4 lsb in range");

    // --- DDJ-FLX4 action table (notes + relative CCs) ---
    const lean::Flx4ActionBind* p0 = lean::flx4FindAction(1, 1, 0x0B);
    check(p0 != nullptr && p0->action == lean::Flx4Action::Play
              && p0->deck == 0,
          "flx4 play deck1");
    check(lean::flx4FindAction(1, 2, 0x0B) != nullptr, "flx4 play deck2");
    check(lean::flx4FindAction(1, 1, 0x0C) != nullptr, "flx4 cue");
    check(lean::flx4FindAction(1, 1, 0x58) != nullptr, "flx4 sync");
    check(lean::flx4FindAction(1, 1, 0x54) != nullptr, "flx4 pfl");
    check(lean::flx4FindAction(1, 1, 0x3F) != nullptr, "flx4 shift");
    check(lean::flx4FindAction(1, 7, 0x46) != nullptr, "flx4 load deck1");
    check(lean::flx4FindAction(1, 7, 0x47) != nullptr, "flx4 load deck2");
    check(lean::flx4FindAction(0, 7, 0x40) != nullptr
              && lean::flx4FindAction(0, 7, 0x40)->action
                  == lean::Flx4Action::BrowseScroll,
          "flx4 browse scroll");
    check(lean::flx4FindAction(0, 1, 0x21) != nullptr, "flx4 jog nudge");
    check(lean::flx4FindAction(0, 1, 0x29) != nullptr
              && lean::flx4FindAction(0, 1, 0x29)->action
                  == lean::Flx4Action::JogSearch,
          "flx4 jog search");
    check(lean::flx4FindAction(1, 1, 0x99) == nullptr, "flx4 action miss");
    check(lean::flx4FindAction(0, 1, 0x0B) == nullptr,
          "flx4 kind separates note from cc"); // 0x0B is a note, not a CC row

    // --- name match + relative decode ---
    check(lean::flx4NameMatches("DDJ-FLX4 MIDI 1"), "name match");
    check(lean::flx4NameMatches("ddj-flx4"), "name match lower");
    check(lean::flx4NameMatches("DDJ-FLX4"), "name match upper");
    check(!lean::flx4NameMatches("DDJ-200"), "name miss other model");
    check(!lean::flx4NameMatches("FLX400"), "name miss prefix");
    check(!lean::flx4NameMatches(nullptr), "name null");
    check(lean::flx4Relative(0x40) == 0, "rel center");
    check(lean::flx4Relative(0x41) == 1, "rel cw");
    check(lean::flx4Relative(0x3F) == -1, "rel ccw");
    check(lean::flx4Relative(0x00) == -64, "rel min");
    check(lean::flx4Relative(0x7F) == 63, "rel max");

    // --- 14-bit fold (MidiGrid::bind14/ingest14) ---
    lean::MidiGrid g14;
    g14.bind14(1, 0x00, 0, lean::MidiTarget::Tempo);
    const int code14 = g14.lookup(1, 0x00);
    check(code14 >= 0 && lean::MidiGrid::gridTarget(code14)
                            == lean::MidiTarget::Tempo
              && lean::MidiGrid::gridDeck(code14) == 0,
          "bind14 packs");
    check(g14.lookup(1, 0x20) < 0, "lsb half not a cell");
    int msbOut = -1;
    check(g14.ingest14(1, 0x00, 64, &msbOut) == (64 << 7) && msbOut == 0x00,
          "ingest14 msb");
    check(g14.ingest14(1, 0x20, 5, &msbOut) == ((64 << 7) | 5)
              && msbOut == 0x00,
          "ingest14 lsb folds into msb cell");
    check(g14.ingest14(1, 0x00, 0) == ((0 << 7) | 5), "msb-only after lsb");
    check(g14.ingest14(2, 0x00, 1) < 0, "other channel not folded");
    check(g14.ingest14(1, 0x30, 1) < 0, "unbound cc not folded");
    // Rebinding the MSB as a plain 7-bit cell drops the pairing.
    g14.bind(1, 0x00, 0, lean::MidiTarget::Fader);
    check(g14.ingest14(1, 0x20, 1) < 0, "rebind drops lsb pairing");
    g14.clear(1, 0x00);
    check(g14.ingest14(1, 0x00, 1) < 0, "clear drops fold");

    // Persisted map accepts the appended Send target (int 10).
    lean::MidiMap sendMap;
    check(sendMap.load("7 23 0 10\n"), "load send target");
    check(sendMap.lookup(7, 23)->target == lean::MidiTarget::Send,
          "send target roundtrip");

    // --- action mailbox (SPSC ring) ---
    lean::Flx4ActionQueue aq;
    lean::Flx4ActionMsg aout[8];
    for (int i = 0; i < 64; ++i) {
        lean::Flx4ActionMsg m;
        m.action = (std::uint8_t)lean::Flx4Action::Play;
        m.deck = (std::uint8_t)(i & 1);
        m.pressed = 1;
        aq.push(m);
    }
    check(aq.droppedCount() == 0, "mailbox full but not overflow");
    lean::Flx4ActionMsg extra;
    extra.action = (std::uint8_t)lean::Flx4Action::Cue;
    check(!aq.push(extra) && aq.droppedCount() == 1, "mailbox overflow drop");
    check(aq.drain(aout, 8) == 8 && aout[0].deck == 0 && aout[1].deck == 1,
          "mailbox drain order");
    int rest = 0, got = 0;
    while ((got = aq.drain(aout, 8)) > 0)
        rest += got;
    check(rest == 56, "mailbox drain rest");
    check(aq.drain(aout, 8) == 0, "mailbox empty");

    // --- soft-takeover pickup ---
    lean::Pickup pk;
    // Not engaged until the hardware reaches/crosses the app value.
    check(pk.process(0, 0.5f, 0.1f) < 0.0f, "pickup below, no cross");
    check(pk.process(0, 0.5f, 0.6f) > 0.0f, "pickup crosses app value");
    check(pk.process(0, 0.5f, 0.2f) > 0.0f, "pickup stays engaged");
    lean::Pickup pk2;
    check(pk2.process(1, 0.5f, 0.5f) > 0.0f, "pickup lands on app value");
    lean::Pickup pk3;
    // Far apart and moving away: never engages, always silent.
    check(pk3.process(2, 0.9f, 0.3f) < 0.0f, "pickup away 1");
    check(pk3.process(2, 0.9f, 0.2f) < 0.0f, "pickup away 2");
    check(pk3.process(2, 0.9f, 0.1f) < 0.0f, "pickup away 3");
    // Same-cell static value equal to app engages immediately (first tick).
    lean::Pickup pk4;
    check(pk4.process(3, 0.0f, 0.0f) >= 0.0f, "pickup equal engages");
    // Endpoint snap-in: hardware at full travel engages on first tick.
    // Engaged ticks return the hardware value (0.0 is a valid apply
    // value; only negatives mean "ignore").
    lean::Pickup pkEndHi;
    check(pkEndHi.process(4, 0.5f, 1.0f) > 0.0f, "pickup endpoint high engages");
    lean::Pickup pkEndLo;
    check(pkEndLo.process(5, 0.5f, 0.0f) >= 0.0f,
          "pickup endpoint low engages");
    // Out-of-range cell passes through unfiltered.
    check(std::abs(pk.process(-1, 0.5f, 0.3f) - 0.3f) < 1e-6f,
          "pickup bad cell passthrough");

    // --- center-detent knob remap (trim, EQ) ---
    check(std::abs(lean::flx4DetentToApp(0.5f) - 0.8f) < 1e-6f,
          "detent center -> app detent");
    check(std::abs(lean::flx4DetentToApp(0.0f) - 0.0f) < 1e-6f,
          "detent low -> app low");
    check(std::abs(lean::flx4DetentToApp(1.0f) - 1.0f) < 1e-6f,
          "detent high -> app high");
    check(std::abs(lean::flx4DetentToApp(0.25f) - 0.4f) < 1e-6f,
          "detent low half linear");
    check(std::abs(lean::flx4DetentToApp(0.75f) - 0.9f) < 1e-6f,
          "detent high half linear");
    check(lean::flx4TargetHasDetent(lean::MidiTarget::Gain), "trim has detent");
    check(lean::flx4TargetHasDetent(lean::MidiTarget::Low), "low has detent");
    check(lean::flx4TargetHasDetent(lean::MidiTarget::Mid), "mid has detent");
    check(lean::flx4TargetHasDetent(lean::MidiTarget::High), "high has detent");
    check(!lean::flx4TargetHasDetent(lean::MidiTarget::Tempo),
          "tempo no detent (fader)");
    check(!lean::flx4TargetHasDetent(lean::MidiTarget::Fader),
          "fader no detent");
    check(!lean::flx4TargetHasDetent(lean::MidiTarget::Xfade),
          "xfade no detent");

    // EQ knobs are inverted (low at bottom, high at top).
    check(lean::flx4TargetNeedsInvert(lean::MidiTarget::Low), "low inverted");
    check(lean::flx4TargetNeedsInvert(lean::MidiTarget::Mid), "mid inverted");
    check(lean::flx4TargetNeedsInvert(lean::MidiTarget::High), "high inverted");
    check(!lean::flx4TargetNeedsInvert(lean::MidiTarget::Gain),
          "trim not inverted");
    check(!lean::flx4TargetNeedsInvert(lean::MidiTarget::Tempo),
          "tempo not inverted");

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
