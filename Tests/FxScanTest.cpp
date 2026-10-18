// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// S9 path-collection gate (JUCE-free): non-recursive *.vst3 discovery over
// a temp-dir fixture with fake bundle names — files, directories, mixed
// case, missing roots, and a nested bundle that must NOT be found.
#include "FxScanPaths.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;

void check(bool cond, const char* name) {
    std::printf("FXSCAN_TEST %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        ++failures;
}

std::string displayOf(const std::string& entry) {
    return entry.substr(0, entry.find('|'));
}

std::string pathOf(const std::string& entry) {
    return entry.substr(entry.find('|') + 1);
}

int run() {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "transit_fxscan_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Fake Bundle.vst3", ec); // bundle = dir
    fs::create_directories(dir / "Upper.VST3", ec);       // case-insensitive
    fs::create_directories(dir / "sub" / "nested.vst3", ec); // not root
    fs::create_directories(dir / "plain.vst3", ec); // dir, not file: any form
    std::ofstream(dir / "notes.txt") << "not a bundle";

    const std::string missing = (dir / "does-not-exist").string();
    const auto entries = lean::collectVst3Bundles({ dir.string(), missing });

    check(entries.size() == 3, "three root bundles, missing root skipped");
    if (entries.size() == 3) {
        // Case-insensitive sort of "name|path": fake bundle < plain < upper.
        check(displayOf(entries[0]) == "Fake Bundle", "display name 0");
        check(displayOf(entries[1]) == "plain", "display name 1");
        check(displayOf(entries[2]) == "Upper", "case-folded suffix stripped");
        bool pathsOk = true;
        bool nestedSeen = false;
        bool txtSeen = false;
        for (const auto& e : entries) {
            const auto p = pathOf(e);
            if (p.find("sub") != std::string::npos)
                nestedSeen = true;
            if (p.find("notes.txt") != std::string::npos)
                txtSeen = true;
            if (p.rfind(dir.string(), 0) != 0)
                pathsOk = false;
            // Suffix is case-insensitive (as the collector accepts them).
            if (p.size() < 5)
                pathsOk = false;
            else {
                std::string sfx = p.substr(p.size() - 5);
                for (char& c : sfx)
                    c = (char)std::tolower((unsigned char)c);
                if (sfx != ".vst3")
                    pathsOk = false;
            }
        }
        check(!nestedSeen, "nested bundle not collected (non-recursive)");
        check(!txtSeen, "non-vst3 files ignored");
        check(pathsOk, "paths stay under the root and end in .vst3");
    }

    check(lean::collectVst3Bundles({}).empty(), "no roots -> empty");
    check(lean::collectVst3Bundles({ missing }).empty(),
          "missing root -> empty");

    fs::remove_all(dir, ec);
    if (failures == 0)
        std::printf("FXSCAN_TEST PASS\n");
    else
        std::printf("FXSCAN_TEST FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    return run();
}
