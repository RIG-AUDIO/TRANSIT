// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "Library.h"

#include "Analyze.h"
#include "Decode.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string_view>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <sqlite3.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace lean {
namespace {

bool exec(Library* self, sqlite3* db, const char* sql) {
    (void)self;
    char* msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
    if (msg)
        sqlite3_free(msg);
    return rc == SQLITE_OK;
}

std::string lower(std::string s) {
    for (char& c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

std::string escapeLike(std::string s) {
    std::string out;
    for (char c : s) {
        if (c == '%' || c == '_' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

// C++20: path::u8string() returns char8_t-based u8string; reinterpret as
// UTF-8 char bytes (identical representation).
std::string toUtf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string((const char*)u8.data(), u8.size());
}

std::filesystem::path fromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// Files above this size are indexed but not analysed: the tempo estimate
// needs the decoded samples in memory, and a decoded float copy of a long
// track is far larger than the file itself.
constexpr long long kMaxAnalyseBytes = 64LL * 1024LL * 1024LL;

// Decodes the file and derives its duration and tempo estimate. Failures
// leave both values unknown (0.0) — the row is still indexed.
bool analyseFile(const std::string& utf8Path, double& bpmOut,
                 double& secondsOut) {
    DecodedAudio decoded;
    std::string error;
    if (!decodeFile(utf8Path, decoded, error))
        return false;
    if (decoded.sampleRate <= 0.0 || decoded.stereo.empty())
        return false;
    const int frames = (int)(decoded.stereo.size() / 2);
    secondsOut = (double)frames / decoded.sampleRate;
    bpmOut = detectBpm(decoded.stereo.data(), frames, decoded.sampleRate);
    return true;
}

} // namespace

Library::~Library() {
    std::lock_guard<std::mutex> lock(mutex);
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Library::open(const std::string& dbPath, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex);
    if (db)
        return true;
    const int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc != SQLITE_OK) {
        error = sqlite3_errmsg(db ? db : nullptr);
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
        return false;
    }
    if (!exec(this, db,
              "CREATE TABLE IF NOT EXISTS tracks("
              "path TEXT PRIMARY KEY, name TEXT, size INTEGER, mtime INTEGER)")) {
        error = "schema failed";
        return false;
    }
    // G6 metadata columns, added in place so index files written before
    // them keep working; unknown values stay 0.
    if (!ensureColumn("tracks", "bpm", "REAL NOT NULL DEFAULT 0")
        || !ensureColumn("tracks", "duration_ms", "INTEGER NOT NULL DEFAULT 0")) {
        error = "schema migration failed";
        return false;
    }
    exec(this, db,
         "CREATE INDEX IF NOT EXISTS idx_tracks_name ON tracks(name COLLATE NOCASE)");
    return true;
}

bool Library::ensureColumn(const char* table, const char* column,
                           const char* decl) {
    const std::string pragma = std::string("PRAGMA table_info(") + table + ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* name = sqlite3_column_text(stmt, 1);
        if (name && std::string_view((const char*)name) == std::string_view(column)) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (found)
        return true;
    const std::string alter = std::string("ALTER TABLE ") + table + " ADD COLUMN "
                             + column + " " + decl;
    return exec(this, db, alter.c_str());
}

bool Library::isAudioFile(const std::string& filename) {
    const std::string t = lower(filename);
    const std::string_view hay(t);
    for (const char* e : { ".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".mp4" }) {
        const std::string_view ext(e);
        if (hay.size() >= ext.size()
            && hay.compare(hay.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    }
    return false;
}

int Library::scanFolder(const std::string& folderUtf8) {
    // G6: every indexed track also carries a tempo estimate and a duration.
    // Files that are unchanged since the last scan keep their stored values;
    // new or modified files are decoded and analysed. The decode happens
    // outside the library lock, so a long scan never blocks message-thread
    // searches (or the UI) for the whole folder.
    struct Entry {
        std::string path;
        std::string name;
        long long size = -1;
        long long mtime = 0;
        double bpm = 0.0;
        double seconds = 0.0;
        bool needsAnalysis = false;
    };
    std::vector<Entry> entries;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!db)
            return -1;
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(fromUtf8(folderUtf8), ec))
            return -1;

        sqlite3_stmt* existing = nullptr;
        if (sqlite3_prepare_v2(db,
                               "SELECT size,mtime,bpm,duration_ms FROM tracks "
                               "WHERE path=?",
                               -1, &existing, nullptr)
            != SQLITE_OK)
            return -1;

        fs::directory_iterator it(fromUtf8(folderUtf8),
                                  fs::directory_options::skip_permission_denied, ec);
        const fs::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const auto& p = it->path();
            const std::string name = toUtf8(p.filename());
            if (!isAudioFile(name))
                continue;
            Entry e;
            e.path = toUtf8(p);
            e.name = name;
            std::error_code ec2;
            e.size = (long long)fs::file_size(p, ec2);
            const auto ft = fs::last_write_time(p, ec2);
            if (!ec2)
                e.mtime = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                              ft.time_since_epoch())
                              .count();

            // Reuse the stored analysis while the file is untouched.
            sqlite3_reset(existing);
            sqlite3_bind_text(existing, 1, e.path.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(existing) == SQLITE_ROW) {
                const long long oldSize =
                    (long long)sqlite3_column_int64(existing, 0);
                const long long oldMtime =
                    (long long)sqlite3_column_int64(existing, 1);
                if (oldSize == e.size && oldMtime == e.mtime) {
                    e.bpm = sqlite3_column_double(existing, 2);
                    e.seconds = (double)sqlite3_column_int64(existing, 3) / 1000.0;
                } else {
                    e.needsAnalysis = true;
                }
            } else {
                e.needsAnalysis = true;
            }
            sqlite3_reset(existing);
            entries.push_back(std::move(e));
        }
        sqlite3_finalize(existing);
    }

    // Analysis outside the lock: no database access, no UI blocking.
    for (auto& e : entries)
        if (e.needsAnalysis && e.size >= 0 && e.size <= kMaxAnalyseBytes)
            analyseFile(e.path, e.bpm, e.seconds);

    std::lock_guard<std::mutex> lock(mutex);
    if (!db)
        return -1;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO tracks(path,name,size,mtime,bpm,duration_ms) "
                           "VALUES(?,?,?,?,?,?) "
                           "ON CONFLICT(path) DO UPDATE SET name=excluded.name,"
                           "size=excluded.size,mtime=excluded.mtime,"
                           "bpm=excluded.bpm,duration_ms=excluded.duration_ms",
                           -1, &stmt, nullptr)
        != SQLITE_OK)
        return -1;

    exec(this, db, "BEGIN");
    int touched = 0;
    for (const auto& e : entries) {
        sqlite3_bind_text(stmt, 1, e.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, e.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, e.size);
        sqlite3_bind_int64(stmt, 4, e.mtime);
        sqlite3_bind_double(stmt, 5, e.bpm);
        sqlite3_bind_int64(stmt, 6, (long long)(e.seconds * 1000.0));
        if (sqlite3_step(stmt) == SQLITE_DONE)
            ++touched;
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    exec(this, db, "COMMIT");
    return touched;
}

std::vector<std::pair<std::string, Library::TrackMeta>>
Library::metadataFor(const std::vector<std::string>& pathsUtf8) {
    std::vector<std::pair<std::string, TrackMeta>> out;
    std::lock_guard<std::mutex> lock(mutex);
    if (!db || pathsUtf8.empty())
        return out;
    // Chunked so the parameter count stays below the SQLite limit.
    constexpr size_t kChunk = 400;
    for (size_t start = 0; start < pathsUtf8.size(); start += kChunk) {
        const size_t count = std::min(kChunk, pathsUtf8.size() - start);
        std::string sql = "SELECT path,bpm,duration_ms FROM tracks WHERE path IN (";
        sql.reserve(sql.size() + count * 3 + 8);
        for (size_t i = 0; i < count; ++i)
            sql += (i == 0) ? "?" : ",?";
        sql += ")";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            continue;
        for (size_t i = 0; i < count; ++i)
            sqlite3_bind_text(stmt, (int)i + 1, pathsUtf8[start + i].c_str(), -1,
                              SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const auto* path = sqlite3_column_text(stmt, 0);
            if (!path)
                continue;
            TrackMeta meta;
            meta.bpm = sqlite3_column_double(stmt, 1);
            meta.seconds = (double)sqlite3_column_int64(stmt, 2) / 1000.0;
            out.emplace_back(std::string((const char*)path), meta);
        }
        sqlite3_finalize(stmt);
    }
    return out;
}

std::vector<std::string> Library::search(const std::string& query) {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lock(mutex);
    if (!db)
        return out;
    sqlite3_stmt* stmt = nullptr;
    const std::string pattern = "%" + escapeLike(query) + "%";
    if (sqlite3_prepare_v2(db,
                           "SELECT path FROM tracks WHERE name LIKE ? ESCAPE '\\' "
                           "ORDER BY name LIMIT 500",
                           -1, &stmt, nullptr)
        != SQLITE_OK)
        return out;
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* t = sqlite3_column_text(stmt, 0);
        if (t)
            out.emplace_back((const char*)t);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Library::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!db)
        return false;
    return exec(this, db, "DELETE FROM tracks");
}

int Library::count() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!db)
        return 0;
    sqlite3_stmt* stmt = nullptr;
    int n = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM tracks", -1, &stmt, nullptr)
        == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            n = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return n;
}

} // namespace lean
