// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Library index gates: scan finds audio files (not other files), search is
// case-insensitive substring, rescan is idempotent, new files appear, and
// concurrent scan/search threads serialize without races or deadlock.
#include "Library.h"
#include "TempDir.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* name) {
    std::printf("LIBRARY_TEST %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        ++failures;
}

bool contains(const std::vector<std::string>& v, const std::string& tail) {
    for (const auto& s : v)
        if (s.size() >= tail.size()
            && s.compare(s.size() - tail.size(), tail.size(), tail) == 0)
            return true;
    return false;
}

// Writes a 16-bit stereo WAV with a click every 1/bpm seconds. A steady
// click train has a clear tempo, which the index analysis must recover.
void writeClickTrack(const std::filesystem::path& path, int rate, double bpm,
                     double seconds) {
    const int frames = (int)((double)rate * seconds);
    const int period = (int)((double)rate * 60.0 / bpm);
    std::vector<unsigned char> bytes;
    auto putU16 = [&bytes](unsigned int v) {
        bytes.push_back((unsigned char)(v & 0xFFu));
        bytes.push_back((unsigned char)((v >> 8) & 0xFFu));
    };
    auto putU32 = [&bytes](unsigned int v) {
        putU16(v & 0xFFFFu);
        putU16((v >> 16) & 0xFFFFu);
    };
    putU32(0x46464952u); // "RIFF"
    putU32((unsigned int)(36 + frames * 4));
    putU32(0x45564157u); // "WAVE"
    putU32(0x20746D66u); // "fmt "
    putU32(16);
    putU16(1);  // PCM
    putU16(2);  // stereo
    putU32((unsigned int)rate);
    putU32((unsigned int)(rate * 4));
    putU16(4);  // block align
    putU16(16); // bits per sample
    putU32(0x61746164u); // "data"
    putU32((unsigned int)(frames * 4));
    for (int i = 0; i < frames; ++i) {
        const bool click = (i % period) < 64;
        const unsigned int v = click ? 12000u : 0u;
        putU16(v);
        putU16(v);
    }
    std::ofstream out(path, std::ios::binary);
    out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
}

int run() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto dir = testutil::privateTempDir("transit_lib_test");
    if (dir.empty()) {
        std::printf("LIBRARY_TEST FAIL: cannot create temp dir\n");
        return 1;
    }
    for (const char* f : { "Alpha Mix.mp3", "beta GROOVE.wav", "Gamma.flac",
                           "notes.txt", "cover.jpg" }) {
        std::ofstream(dir / f, std::ios::binary) << "data";
    }

    lean::Library lib;
    std::string error;
    if (!lib.open((dir / "lib.db").string(), error)) {
        std::printf("LIBRARY_TEST FAIL open: %s\n", error.c_str());
        return 1;
    }
    check(lib.scanFolder(dir.string()) == 3, "scan finds 3 audio files");
    check(lib.count() == 3, "count 3");
    check(contains(lib.search("mix"), "Alpha Mix.mp3"), "search mix");
    check(contains(lib.search("GROOVE"), "beta GROOVE.wav"), "search case");
    check(contains(lib.search("a"), "Alpha Mix.mp3")
              && contains(lib.search("a"), "beta GROOVE.wav")
              && contains(lib.search("a"), "Gamma.flac"),
          "search broad");
    check(lib.search("zzz").empty(), "search miss empty");

    // Rescan idempotent; new file appears.
    check(lib.scanFolder(dir.string()) == 3, "rescan stable");
    check(lib.count() == 3, "count still 3");
    std::ofstream(dir / "Delta.m4a", std::ios::binary) << "data";
    lib.scanFolder(dir.string());
    check(lib.count() == 4, "new file indexed");
    check(contains(lib.search("delta"), "Delta.m4a"), "search new file");

    // Missing folder errors cleanly.
    check(lib.scanFolder((dir / "nope").string()) == -1, "missing folder -1");

    // Concurrency (review S6/S11): message-thread-style searches racing a
    // background scan, as in production (worker scans, timer searches). The
    // internal mutex must serialize them; a data race would fail under the
    // sanitizer job, a deadlock would hit the CI timeout.
    {
        std::atomic<bool> stop{ false };
        std::atomic<int> served{ 0 }; // completed calls from reader threads
        std::atomic<int> hits{ 0 };   // reader searches with results
        std::thread reader([&] {
            do {
                const auto r = lib.search("a");
                if (!r.empty())
                    hits.fetch_add(1, std::memory_order_seq_cst);
                served.fetch_add(1, std::memory_order_seq_cst);
            } while (!stop.load(std::memory_order_seq_cst));
        });
        std::thread reader2([&] {
            do {
                const int c = lib.count();
                if (c < 0)
                    hits.store(-1, std::memory_order_seq_cst);
                served.fetch_add(1, std::memory_order_seq_cst);
            } while (!stop.load(std::memory_order_seq_cst));
        });
        int badScans = 0;
        for (int i = 0; i < 20; ++i)
            if (lib.scanFolder(dir.string()) != 4)
                ++badScans;
        stop.store(true, std::memory_order_seq_cst);
        reader.join();
        reader2.join();
        check(badScans == 0, "concurrent scans all indexed 4");
        check(served.load(std::memory_order_seq_cst) >= 2,
              "concurrent readers served");
        check(hits.load(std::memory_order_seq_cst) > 0,
              "concurrent search returned rows");
        check(lib.count() == 4, "count stable after concurrent scans");
    }

    // G6 round 2: the index also stores a tempo estimate and a duration.
    // A real click track (120 BPM) makes both measurable end to end.
    const auto clickPath = dir / "click120.wav";
    writeClickTrack(clickPath, 44100, 120.0, 5.0);
    check(lib.scanFolder(dir.string()) == 5, "scan indexed the click track");
    check(lib.count() == 5, "count 5 after the click track");

    const auto meta = lib.metadataFor({ clickPath.string() });
    check(meta.size() == 1, "metadata found for the indexed track");
    if (meta.size() == 1) {
        check(meta[0].second.bpm > 118.0 && meta[0].second.bpm < 122.0,
              "tempo estimate ~120 BPM");
        check(meta[0].second.seconds > 4.9 && meta[0].second.seconds < 5.1,
              "duration ~5 s");
    }
    const auto missing =
        lib.metadataFor({ (dir / "not-indexed.wav").string() });
    check(missing.empty(), "unknown path omitted from metadata");
    const auto garbage = lib.metadataFor({ (dir / "Alpha Mix.mp3").string() });
    check(garbage.size() == 1 && !(garbage[0].second.bpm > 0.0)
              && !(garbage[0].second.seconds > 0.0),
          "unreadable file stays unknown");

    fs::remove_all(dir, ec);
    if (failures == 0)
        std::printf("LIBRARY_TEST PASS\n");
    else
        std::printf("LIBRARY_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
