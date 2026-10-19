// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <mutex>
#include <string>
#include <utility>
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

    // Metadata for indexed tracks (G6). `bpm` is 0.0 and `seconds` is 0.0
    // when the track could not be analysed (unreadable file, unsupported
    // codec, or a file above the analysis size cap).
    struct TrackMeta {
        double bpm = 0.0;
        double seconds = 0.0;
    };

    // Batch lookup of indexed metadata for the given paths (UTF-8). Paths
    // that are not in the index are omitted from the result.
    std::vector<std::pair<std::string, TrackMeta>> metadataFor(
        const std::vector<std::string>& pathsUtf8);

    static bool isAudioFile(const std::string& filename);

private:
    // Adds a column to an existing table when it is missing (schema
    // migration for index files created before the metadata columns).
    bool ensureColumn(const char* table, const char* column, const char* decl);

    sqlite3* db = nullptr; // set once in open(), before any worker handoff
    mutable std::mutex mutex; // serializes every sqlite use below open()
};

} // namespace lean
