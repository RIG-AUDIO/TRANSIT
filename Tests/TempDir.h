// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Private per-process temp directory for tests (CWE-377). The name embeds
// a per-process tick so it cannot be pre-created by another user; creation
// is exclusive (never reuses an existing path) and the directory is then
// tightened to owner-only permissions. Returns an empty path on failure.
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace testutil {

inline std::filesystem::path privateTempDir(const char* leaf) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto dir = fs::temp_directory_path(ec);
    if (ec)
        return {};
    const auto tick =
        (long long)std::chrono::steady_clock::now().time_since_epoch().count();
    dir /= std::string(leaf) + "_" + std::to_string(tick);
    fs::remove_all(dir, ec);
    ec.clear();
    // Exclusive: false (or an error) means the path already existed — never
    // enter a directory this process did not create.
    if (!fs::create_directory(dir, ec) || ec)
        return {};
    fs::permissions(dir, fs::perms::owner_all, ec);
    if (ec)
        return {};
    return dir;
}

} // namespace testutil
