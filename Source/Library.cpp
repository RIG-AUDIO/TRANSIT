// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "Library.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string_view>

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
    exec(this, db,
         "CREATE INDEX IF NOT EXISTS idx_tracks_name ON tracks(name COLLATE NOCASE)");
    return true;
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
    std::lock_guard<std::mutex> lock(mutex);
    if (!db)
        return -1;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(fromUtf8(folderUtf8), ec))
        return -1;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO tracks(path,name,size,mtime) VALUES(?,?,?,?) "
                           "ON CONFLICT(path) DO UPDATE SET name=excluded.name,"
                           "size=excluded.size,mtime=excluded.mtime",
                           -1, &stmt, nullptr)
        != SQLITE_OK)
        return -1;

    exec(this, db, "BEGIN");
    int touched = 0;
    fs::directory_iterator it(fromUtf8(folderUtf8),
                              fs::directory_options::skip_permission_denied, ec);
    const fs::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto& p = it->path();
        const std::string name = toUtf8(p.filename());
        if (!isAudioFile(name))
            continue;
        const std::string full = toUtf8(p);
        long long size = -1, mtime = 0;
        std::error_code ec2;
        size = (long long)fs::file_size(p, ec2);
        const auto ft = fs::last_write_time(p, ec2);
        if (!ec2)
            mtime = std::chrono::duration_cast<std::chrono::seconds>(
                        ft.time_since_epoch())
                        .count();
        sqlite3_bind_text(stmt, 1, full.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, size);
        sqlite3_bind_int64(stmt, 4, mtime);
        if (sqlite3_step(stmt) == SQLITE_DONE)
            ++touched;
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    exec(this, db, "COMMIT");
    return touched;
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
