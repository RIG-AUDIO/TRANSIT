// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

// JUCE-free VST3 bundle *path collection* (review S9). Given root
// directories, list non-recursive "*.vst3" entries (files or directories)
// as "display-name|full-path" strings, sorted case-insensitively — the
// exact wire format the FX menu consumed from the old JUCE walk. No
// loading, no side effects: unit-testable against a temp-dir fixture.
namespace lean {

inline std::string toUtf8Path(const std::filesystem::path& p) {
    // C++20: path::u8string() is char8_t-based; reinterpret as UTF-8 bytes
    // (identical representation) — same pattern as Library.cpp.
    const std::u8string u8 = p.u8string();
    return std::string((const char*)u8.data(), u8.size());
}

inline std::vector<std::string> collectVst3Bundles(
    const std::vector<std::string>& roots) {
    std::vector<std::string> entries;
    namespace fs = std::filesystem;
    for (const auto& root : roots) {
        std::error_code ec;
        fs::directory_iterator it(
            fs::path(std::u8string(root.begin(), root.end())),
            fs::directory_options::skip_permission_denied, ec);
        if (ec)
            continue; // missing or unreadable roots are normal
        const fs::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::path p = it->path();
            const std::string fname = toUtf8Path(p.filename());
            if (fname.size() < 6)
                continue;
            std::string suffix = fname.substr(fname.size() - 5);
            for (char& c : suffix)
                c = (char)std::tolower((unsigned char)c);
            if (suffix != ".vst3")
                continue;
            entries.push_back(
                fname.substr(0, fname.size() - 5) + "|" + toUtf8Path(p));
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const std::string& a, const std::string& b) {
                  std::string la = a, lb = b;
                  for (char& c : la)
                      c = (char)std::tolower((unsigned char)c);
                  for (char& c : lb)
                      c = (char)std::tolower((unsigned char)c);
                  return la < lb;
              });
    return entries;
}

} // namespace lean
