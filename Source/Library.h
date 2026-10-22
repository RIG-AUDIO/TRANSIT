// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace lean {

// Track index over a SQLite database (public-domain SQLite, see README).
// Message/worker-thread use only. All paths are UTF-8. All methods serialize
// on an internal mutex so a background scan and message-thread searches can
// interleave safely (never called from the audio thread).
class Library {
public:
    Library() = default;
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    bool open(const std::string& dbPath, std::string& error);
    bool isOpen() const { return db != nullptr; }

    // Scan one folder (non-recursive) for audio files. Returns the number
    // of rows inserted or updated, or -1 on error.
    int scanFolder(const std::string& folderUtf8);

    // Case-insensitive substring search on filenames. Full paths, max 500.
    std::vector<std::string> search(const std::string& query);
    int count();
    bool clear();

    static bool isAudioFile(const std::string& filename);

private:
    sqlite3* db = nullptr; // set once in open(), before any worker handoff
    mutable std::mutex mutex; // serializes every sqlite use below open()
};

} // namespace lean
