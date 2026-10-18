// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Library index gates: scan finds audio files (not other files), search is
// case-insensitive substring, rescan is idempotent, new files appear, and
// concurrent scan/search threads serialize without races or deadlock.
#include "Library.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

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

int run() {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "transit_lib_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
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
                    hits.fetch_add(1, std::memory_order_relaxed);
                served.fetch_add(1, std::memory_order_relaxed);
            } while (!stop.load(std::memory_order_acquire));
        });
        std::thread reader2([&] {
            do {
                const int c = lib.count();
                if (c < 0)
                    hits.store(-1, std::memory_order_relaxed);
                served.fetch_add(1, std::memory_order_relaxed);
            } while (!stop.load(std::memory_order_acquire));
        });
        int badScans = 0;
        for (int i = 0; i < 20; ++i)
            if (lib.scanFolder(dir.string()) != 4)
                ++badScans;
        stop.store(true, std::memory_order_release);
        reader.join();
        reader2.join();
        check(badScans == 0, "concurrent scans all indexed 4");
        check(served.load(std::memory_order_relaxed) >= 2,
              "concurrent readers served");
        check(hits.load(std::memory_order_relaxed) > 0,
              "concurrent search returned rows");
        check(lib.count() == 4, "count stable after concurrent scans");
    }

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
