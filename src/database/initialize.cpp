#include "fileweaver/application/initialize.hpp"
#include "fileweaver/application/index.hpp"
#include "fileweaver/application/inspect.hpp"
#include "fileweaver/application/refresh.hpp"
#include "fileweaver/application/search.hpp"
#include "fileweaver/core/storage_error.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <system_error>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fileweaver {
namespace {
using Connection = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

std::optional<std::int64_t> creation_nanoseconds(const std::filesystem::path &path) {
#ifdef _WIN32
    struct CloseFile {
        void operator()(void *handle) const noexcept { CloseHandle(handle); }
    };
    const auto raw = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    const std::unique_ptr<void, CloseFile> handle(raw);
    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(handle.get(), &information) == 0 ||
        (information.dwFileAttributes & (static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT) |
                                         static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY))) != 0) {
        return std::nullopt;
    }
    const auto ticks =
        (static_cast<std::uint64_t>(information.ftCreationTime.dwHighDateTime) << 32U) |
        information.ftCreationTime.dwLowDateTime;
    constexpr std::uint64_t epoch = 116444736000000000ULL;
    const auto magnitude = ticks >= epoch ? ticks - epoch : epoch - ticks;
    if (magnitude > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) / 100) {
        return std::nullopt;
    }
    const auto nanoseconds = static_cast<std::int64_t>(magnitude * 100);
    return ticks >= epoch ? nanoseconds : -nanoseconds;
#else
    static_cast<void>(path);
    return std::nullopt;
#endif
}

void execute(sqlite3 *db, const char *sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw StorageError(std::string("Database initialization failed: ") + sqlite3_errmsg(db), 5);
    }
}

int scalar(sqlite3 *db, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    const int result = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
    Statement statement(raw, sqlite3_finalize);
    if (result != SQLITE_OK || sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw StorageError(std::string("Cannot inspect schema: ") + sqlite3_errmsg(db), 5);
    }
    return sqlite3_column_int(statement.get(), 0);
}

std::string utf8(const std::filesystem::path &path) {
    try {
        const auto value = path.generic_u8string();
        return {value.begin(), value.end()};
    } catch (const std::system_error &error) {
        bool conversion_failed = error.code() == std::errc::illegal_byte_sequence;
#ifdef _WIN32
        conversion_failed =
            conversion_failed ||
            error.code() == std::error_code(static_cast<int>(ERROR_NO_UNICODE_TRANSLATION),
                                            std::system_category());
#endif
        if (!conversion_failed) {
            throw;
        }
        // Never attempt another encoding conversion while reporting this failure.
        // Escaping native code units also keeps diagnostics valid UTF-8 and one line.
        constexpr char hex[] = "0123456789ABCDEF";
        std::string escaped;
        for (const auto character : path.native()) {
            const auto unit = static_cast<std::uint32_t>(
                static_cast<std::make_unsigned_t<std::filesystem::path::value_type>>(character));
            if (unit >= 32 && unit < 127) {
                escaped += static_cast<char>(unit);
            } else {
                escaped += "\\u";
                for (unsigned digit = 0; digit < 4; ++digit) {
                    escaped += hex[(unit >> (12U - digit * 4U)) & 15U];
                }
            }
        }
        throw StorageError("Cannot encode filesystem path as UTF-8 (character conversion failed). "
                           "Path (escaped native code units): " +
                               escaped,
                           1);
    }
}

Statement prepare(sqlite3 *db, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    const int result = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
    Statement statement(raw, sqlite3_finalize);
    if (result != SQLITE_OK) {
        throw StorageError(std::string("Cannot prepare index statement: ") + sqlite3_errmsg(db), 5);
    }
    return statement;
}

void bind_text(sqlite3_stmt *statement, int position, const std::string &value) {
    if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        sqlite3_bind_text(statement, position, value.c_str(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        throw StorageError("Cannot bind index text", 1);
    }
}

void bind_integer(sqlite3_stmt *statement, int position, std::int64_t value) {
    if (sqlite3_bind_int64(statement, position, value) != SQLITE_OK) {
        throw StorageError("Cannot bind index integer", 1);
    }
}

void finish(sqlite3_stmt *statement) {
    if (sqlite3_step(statement) != SQLITE_DONE) {
        throw StorageError(
            std::string("Index write failed: ") + sqlite3_errmsg(sqlite3_db_handle(statement)), 1);
    }
    if (sqlite3_reset(statement) != SQLITE_OK || sqlite3_clear_bindings(statement) != SQLITE_OK) {
        throw StorageError("Cannot reset index statement", 1);
    }
}

std::int64_t checked_count(std::uint64_t value) {
    if (value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
        throw StorageError("Scan count exceeds supported database range", 1);
    }
    return static_cast<std::int64_t>(value);
}

template <class Duration>
auto file_time_to_sys(
    const std::chrono::time_point<std::filesystem::file_time_type::clock, Duration> &value) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    return std::chrono::clock_cast<std::chrono::system_clock>(value);
#else
    return std::filesystem::file_time_type::clock::to_sys(value);
#endif
}

std::int64_t modified_nanoseconds(std::filesystem::file_time_type value) {
    using WideSeconds = std::chrono::duration<long double>;
    using FileClock = std::filesystem::file_time_type::clock;
    const std::chrono::time_point<FileClock, WideSeconds> wide(
        std::chrono::duration_cast<WideSeconds>(value.time_since_epoch()));
    const auto nanos =
        std::chrono::duration<long double, std::nano>(file_time_to_sys(wide).time_since_epoch())
            .count();
    if (!std::isfinite(nanos) ||
        nanos >= static_cast<long double>((std::numeric_limits<std::int64_t>::max)()) ||
        nanos <= static_cast<long double>((std::numeric_limits<std::int64_t>::min)())) {
        throw StorageError("File timestamp exceeds supported database range", 1);
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               file_time_to_sys(value).time_since_epoch())
        .count();
}

void verify_policy(sqlite3 *db, std::int64_t root_id, const IndexRequest &request) {
    if (!request.expected_root_id) {
        return;
    }
    if (*request.expected_root_id != root_id) {
        throw StorageError("Root registration changed; retry refresh", 1);
    }
    auto policy = prepare(db, "SELECT maximum_depth FROM root_scan_options WHERE root_id=?1");
    bind_integer(policy.get(), 1, root_id);
    if (sqlite3_step(policy.get()) != SQLITE_ROW ||
        sqlite3_column_int64(policy.get(), 0) !=
            static_cast<std::int64_t>(request.scan.maximum_depth)) {
        throw StorageError("Saved scan policy changed; retry refresh", 1);
    }
    auto paths = prepare(db, "SELECT path FROM root_exclusions WHERE root_id=?1 ORDER BY ordinal");
    bind_integer(paths.get(), 1, root_id);
    std::size_t ordinal = 0;
    for (;;) {
        const int step = sqlite3_step(paths.get());
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            throw StorageError("Cannot read saved exclusions", 5);
        }
        const auto *text = sqlite3_column_text(paths.get(), 0);
        const int length = sqlite3_column_bytes(paths.get(), 0);
        if (ordinal >= request.scan.excluded_paths.size() || text == nullptr || length < 0 ||
            length > 131072 ||
            std::string(reinterpret_cast<const char *>(text), static_cast<std::size_t>(length)) !=
                utf8(request.scan.excluded_paths[ordinal])) {
            throw StorageError("Saved exclusions changed; retry refresh", 1);
        }
        ++ordinal;
    }
    if (ordinal != request.scan.excluded_paths.size()) {
        throw StorageError("Saved exclusions changed; retry refresh", 1);
    }
}
Connection open_inspection(const std::filesystem::path &path, std::stop_token &stop) {
    if (path.empty() || utf8(path).find('\0') != std::string::npos) {
        throw StorageError("An explicit database path is required", 2);
    }
    if (stop.stop_requested()) {
        throw StorageError("Inspection cancelled", 1);
    }
    std::error_code error;
    const bool present = std::filesystem::exists(path, error);
    if (error) {
        throw StorageError("Cannot inspect database path", 3);
    }
    if (!present) {
        throw StorageError("Index is not initialized; run init --database first", 4);
    }
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open_v2(utf8(path).c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    Connection db(raw, sqlite3_close);
    if (opened != SQLITE_OK) {
        throw StorageError("Cannot open inspection database", 3);
    }
    if (sqlite3_busy_timeout(db.get(), 5000) != SQLITE_OK) {
        throw StorageError("Cannot configure inspection timeout", 1);
    }
    execute(db.get(), "BEGIN");
    const int version = scalar(db.get(), "PRAGMA user_version");
    if (version != 2 && version != 3) {
        throw StorageError("Inspection requires schema v2 or v3", 5);
    }
    sqlite3_progress_handler(
        db.get(), 1000,
        [](void *context) {
            return static_cast<std::stop_token *>(context)->stop_requested() ? 1 : 0;
        },
        &stop);
    return db;
}

bool inspection_row(sqlite3_stmt *row, const std::stop_token &stop) {
    if (stop.stop_requested()) {
        throw StorageError("Inspection cancelled", 1);
    }
    const int step = sqlite3_step(row);
    if (step == SQLITE_DONE) {
        return false;
    }
    if (step == SQLITE_INTERRUPT) {
        throw StorageError("Inspection cancelled", 1);
    }
    if (step != SQLITE_ROW) {
        throw StorageError("Cannot read catalog metadata", 5);
    }
    return true;
}

std::int64_t inspection_integer(sqlite3_stmt *row, int column) {
    if (sqlite3_column_type(row, column) != SQLITE_INTEGER) {
        throw StorageError("Invalid catalog integer", 5);
    }
    return sqlite3_column_int64(row, column);
}

std::string inspection_text(sqlite3_stmt *row, int column, std::size_t &bytes) {
    if (sqlite3_column_type(row, column) != SQLITE_TEXT) {
        throw StorageError("Invalid catalog text", 5);
    }
    const int length = sqlite3_column_bytes(row, column);
    const auto *text = sqlite3_column_text(row, column);
    if (length < 0 || length > 131072 || text == nullptr) {
        throw StorageError("Invalid catalog text length", 5);
    }
    bytes += static_cast<std::size_t>(length);
    if (bytes > std::size_t{16} * 1024 * 1024) {
        throw StorageError("Root inventory exceeds 16 MiB text budget", 1);
    }
    return {reinterpret_cast<const char *>(text), static_cast<std::size_t>(length)};
}
} // namespace

void initialize_database(const std::filesystem::path &path) {
    const auto encoded = path.u8string();
    const std::string filename(encoded.begin(), encoded.end());
    if (filename.empty() || filename.find('\0') != std::string::npos || filename == ":memory:") {
        throw StorageError("A persistent database path is required", 2);
    }
    sqlite3 *raw = nullptr;
    const int result = sqlite3_open_v2(filename.c_str(), &raw,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    Connection db(raw, sqlite3_close);
    if (result != SQLITE_OK) {
        throw StorageError("Cannot open database; check path, parent directory and permissions", 3);
    }
    if (sqlite3_busy_timeout(db.get(), 5000) != SQLITE_OK) {
        throw StorageError("Cannot configure database timeout", 5);
    }
    execute(db.get(), "PRAGMA foreign_keys=ON");
    execute(db.get(), "BEGIN IMMEDIATE");
    try {
        const int version = scalar(db.get(), "PRAGMA user_version");
        if (version == 0) {
            if (scalar(db.get(), "SELECT count(*) FROM sqlite_schema WHERE type='table' "
                                 "AND name NOT LIKE 'sqlite_%'") != 0) {
                throw StorageError("Refusing to initialize an unrelated database", 5);
            }
            execute(db.get(), "CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT NOT NULL)");
            execute(db.get(), "PRAGMA user_version=1");
        } else if (version != 1 && version != 2 && version != 3) {
            throw StorageError("Unsupported database schema version", 5);
        }
        execute(db.get(), "SELECT key,value FROM settings LIMIT 0");
        if (scalar(db.get(), "SELECT count(*) FROM pragma_table_info('settings')") != 2 ||
            scalar(db.get(), "SELECT count(*) FROM pragma_table_info('settings') "
                             "WHERE (name='key' AND type='TEXT' AND pk=1) OR "
                             "(name='value' AND type='TEXT' AND \"notnull\"=1 AND pk=0)") != 2) {
            throw StorageError("Invalid settings schema", 5);
        }
        if (version < 2) {
            execute(db.get(),
                    "CREATE TABLE roots("
                    "id INTEGER PRIMARY KEY, normalized_path TEXT NOT NULL UNIQUE,"
                    "display_path TEXT NOT NULL, added_at INTEGER NOT NULL,"
                    "last_scan_at INTEGER, last_scan_status TEXT NOT NULL DEFAULT 'never' "
                    "CHECK(last_scan_status IN "
                    "('never','running','completed','incomplete','cancelled','failed')));"
                    "CREATE TABLE scans("
                    "id INTEGER PRIMARY KEY, root_id INTEGER NOT NULL REFERENCES roots(id),"
                    "started_at INTEGER NOT NULL, finished_at INTEGER,"
                    "status TEXT NOT NULL CHECK(status IN "
                    "('running','completed','incomplete','cancelled','failed')),"
                    "scanned_count INTEGER NOT NULL DEFAULT 0 CHECK(scanned_count>=0),"
                    "new_count INTEGER NOT NULL DEFAULT 0 CHECK(new_count>=0),"
                    "updated_count INTEGER NOT NULL DEFAULT 0 CHECK(updated_count>=0),"
                    "missing_count INTEGER NOT NULL DEFAULT 0 CHECK(missing_count>=0),"
                    "skipped_count INTEGER NOT NULL DEFAULT 0 CHECK(skipped_count>=0));"
                    "CREATE TABLE files("
                    "id INTEGER PRIMARY KEY, root_id INTEGER NOT NULL REFERENCES roots(id),"
                    "normalized_path TEXT NOT NULL, display_path TEXT NOT NULL,"
                    "name TEXT NOT NULL, extension TEXT NOT NULL,"
                    "size_bytes INTEGER NOT NULL CHECK(size_bytes>=0),"
                    "created_at INTEGER, modified_at INTEGER NOT NULL, indexed_at INTEGER NOT NULL,"
                    "file_type TEXT NOT NULL DEFAULT 'regular' CHECK(file_type='regular'),"
                    "is_present INTEGER NOT NULL DEFAULT 1 CHECK(is_present IN (0,1)),"
                    "sha256 TEXT, hash_metadata_version INTEGER,"
                    "UNIQUE(root_id,normalized_path));"
                    "CREATE INDEX files_name ON files(name);"
                    "CREATE INDEX files_root_presence ON files(root_id,is_present);"
                    "CREATE INDEX scans_root ON scans(root_id,id);"
                    "PRAGMA user_version=2");
        }
        execute(db.get(),
                "SELECT id,normalized_path,display_path,added_at,last_scan_at,last_scan_status "
                "FROM roots LIMIT 0");
        execute(db.get(), "SELECT "
                          "id,root_id,started_at,finished_at,status,scanned_count,new_count,"
                          "updated_count,missing_count,skipped_count FROM scans LIMIT 0");
        execute(
            db.get(),
            "SELECT "
            "id,root_id,normalized_path,display_path,name,extension,size_bytes,created_at,modified_"
            "at,indexed_at,file_type,is_present,sha256,hash_metadata_version FROM files LIMIT 0");
        if (version < 3) {
            execute(
                db.get(),
                "CREATE TABLE root_scan_options(root_id INTEGER PRIMARY KEY REFERENCES roots(id),"
                "maximum_depth INTEGER NOT NULL CHECK(maximum_depth BETWEEN 1 AND 128));"
                "CREATE TABLE root_exclusions(root_id INTEGER NOT NULL REFERENCES roots(id),"
                "ordinal INTEGER NOT NULL CHECK(ordinal>=0),path TEXT NOT NULL "
                "CHECK(length(path)>0),"
                "PRIMARY KEY(root_id,ordinal)); PRAGMA user_version=3");
        }
        execute(db.get(), "SELECT root_id,maximum_depth FROM root_scan_options LIMIT 0");
        execute(db.get(), "SELECT root_id,ordinal,path FROM root_exclusions LIMIT 0");
        execute(db.get(), "COMMIT");
    } catch (...) {
        sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    execute(db.get(), "PRAGMA journal_mode=WAL");
}

IndexSummary index_directory(const IndexRequest &request,
                             const std::function<std::int64_t()> &clock,
                             const std::stop_token &stop) {
    if (!clock) {
        throw std::invalid_argument("An index clock is required");
    }
    // Validate root/options without enumerating before opening storage.
    std::stop_source validation;
    validation.request_stop();
    static_cast<void>(
        scan_directory(request.root, request.scan, [](const auto &) {}, validation.get_token()));
    std::error_code error;
    const auto root = std::filesystem::canonical(request.root, error);
    if (error) {
        throw StorageError("Cannot resolve index root", 3);
    }
    if (request.database.empty() || utf8(request.database).find('\0') != std::string::npos) {
        throw StorageError("An explicit database path without null characters is required", 2);
    }
    const bool present = std::filesystem::exists(request.database, error);
    if (error) {
        throw StorageError("Cannot inspect index database", 3);
    }
    if (!present) {
        throw StorageError("Index is not initialized; run init --database first", 4);
    }
    const auto database = std::filesystem::canonical(request.database, error);
    if (error) {
        throw StorageError("Cannot resolve index database", 3);
    }
    sqlite3 *raw = nullptr;
    const auto filename = utf8(database);
    const int opened = sqlite3_open_v2(filename.c_str(), &raw, SQLITE_OPEN_READWRITE, nullptr);
    Connection db(raw, sqlite3_close);
    if (opened != SQLITE_OK) {
        throw StorageError("Cannot open index database", 3);
    }
    if (sqlite3_busy_timeout(db.get(), 5000) != SQLITE_OK) {
        throw StorageError("Cannot configure index timeout", 1);
    }
    execute(db.get(), "PRAGMA foreign_keys=ON");
    if (scalar(db.get(), "PRAGMA user_version") != 3) {
        throw StorageError("Index requires schema v3; run init --database to upgrade", 5);
    }
    auto options = request.scan;
    options.excluded_paths.push_back(database);
    for (const auto *suffix : {"-wal", "-shm", "-journal"}) {
        auto sidecar = database;
        sidecar += suffix;
        options.excluded_paths.push_back(std::move(sidecar));
    }
    const auto started = clock();
    execute(db.get(), "BEGIN IMMEDIATE");
    std::int64_t root_id = 0;
    std::int64_t scan_id = 0;
    try {
        auto add_root =
            prepare(db.get(), "INSERT INTO roots(normalized_path,display_path,added_at) "
                              "VALUES(?1,?2,?3) ON CONFLICT(normalized_path) DO NOTHING");
        bind_text(add_root.get(), 1, utf8(root));
        bind_text(add_root.get(), 2, utf8(root));
        bind_integer(add_root.get(), 3, started);
        finish(add_root.get());
        auto find_root = prepare(db.get(), "SELECT id FROM roots WHERE normalized_path=?1");
        bind_text(find_root.get(), 1, utf8(root));
        if (sqlite3_step(find_root.get()) != SQLITE_ROW) {
            throw StorageError("Cannot find index root", 1);
        }
        root_id = sqlite3_column_int64(find_root.get(), 0);
        verify_policy(db.get(), root_id, request);
        if (request.scan.excluded_paths.size() > 10000) {
            throw StorageError("Too many exclusion paths (maximum 10000)", 2);
        }
        auto policy = prepare(
            db.get(), "INSERT INTO root_scan_options(root_id,maximum_depth) VALUES(?1,?2) "
                      "ON CONFLICT(root_id) DO UPDATE SET maximum_depth=excluded.maximum_depth");
        bind_integer(policy.get(), 1, root_id);
        bind_integer(policy.get(), 2, static_cast<std::int64_t>(request.scan.maximum_depth));
        finish(policy.get());
        auto clear_exclusions = prepare(db.get(), "DELETE FROM root_exclusions WHERE root_id=?1");
        bind_integer(clear_exclusions.get(), 1, root_id);
        finish(clear_exclusions.get());
        auto exclusion =
            prepare(db.get(), "INSERT INTO root_exclusions(root_id,ordinal,path) VALUES(?1,?2,?3)");
        std::size_t policy_bytes = 0;
        for (std::size_t ordinal = 0; ordinal < request.scan.excluded_paths.size(); ++ordinal) {
            const auto path = utf8(request.scan.excluded_paths[ordinal]);
            policy_bytes += path.size();
            if (path.size() > 131072 || policy_bytes > std::size_t{16} * 1024 * 1024) {
                throw StorageError("Exclusion policy exceeds supported text budget", 2);
            }
            bind_integer(exclusion.get(), 1, root_id);
            bind_integer(exclusion.get(), 2, static_cast<std::int64_t>(ordinal));
            bind_text(exclusion.get(), 3, path);
            finish(exclusion.get());
        }
        auto add_scan = prepare(
            db.get(), "INSERT INTO scans(root_id,started_at,status) VALUES(?1,?2,'running')");
        bind_integer(add_scan.get(), 1, root_id);
        bind_integer(add_scan.get(), 2, started);
        finish(add_scan.get());
        scan_id = sqlite3_last_insert_rowid(db.get());
        auto running = prepare(db.get(), "UPDATE roots SET last_scan_status='running' WHERE id=?1");
        bind_integer(running.get(), 1, root_id);
        finish(running.get());
        execute(db.get(), "COMMIT");
    } catch (...) {
        sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    IndexSummary summary;
    const auto record_status = [&](const char *status) {
        auto update =
            prepare(db.get(),
                    "UPDATE scans SET finished_at=?1,status=?2,scanned_count=?3,"
                    "new_count=?4,updated_count=?5,missing_count=?6,skipped_count=?7 WHERE id=?8");
        const auto ended = clock();
        bind_integer(update.get(), 1, ended);
        bind_text(update.get(), 2, status);
        bind_integer(update.get(), 3, checked_count(summary.scan.files));
        bind_integer(update.get(), 4, summary.added);
        bind_integer(update.get(), 5, summary.updated);
        bind_integer(update.get(), 6, summary.missing);
        bind_integer(update.get(), 7, checked_count(summary.scan.skipped));
        bind_integer(update.get(), 8, scan_id);
        finish(update.get());
        auto update_root =
            prepare(db.get(), "UPDATE roots SET last_scan_at=?1,last_scan_status=?2 WHERE id=?3");
        bind_integer(update_root.get(), 1, ended);
        bind_text(update_root.get(), 2, status);
        bind_integer(update_root.get(), 3, root_id);
        finish(update_root.get());
    };
    try {
        execute(db.get(), "BEGIN IMMEDIATE; CREATE TEMP TABLE seen(path TEXT PRIMARY KEY)");
        verify_policy(db.get(), root_id, request);
        auto seen = prepare(db.get(), "INSERT INTO seen VALUES(?1)");
        auto existing =
            prepare(db.get(), "SELECT size_bytes,modified_at,is_present,created_at FROM files "
                              "WHERE root_id=?1 AND normalized_path=?2");
        auto write = prepare(
            db.get(),
            "INSERT INTO "
            "files(root_id,normalized_path,display_path,name,extension,size_bytes,modified_at,"
            "indexed_at,created_at) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) ON CONFLICT(root_id,normalized_path) DO UPDATE SET "
            "display_path=excluded.display_path,name=excluded.name,extension=excluded.extension,"
            "size_bytes=excluded.size_bytes,modified_at=excluded.modified_at,indexed_at=excluded."
            "indexed_at,"
            "created_at=coalesce(excluded.created_at,files.created_at),"
            "is_present=1,sha256=NULL,hash_metadata_version=NULL");
        std::uint64_t database_aliases = 0;
        summary.scan = scan_directory(
            root, options,
            [&](const FileMetadata &file) {
                std::error_code identity_error;
                const bool own_database =
                    std::filesystem::equivalent(file.path, database, identity_error);
                if (identity_error) {
                    throw StorageError("Cannot revalidate file identity during indexing", 1);
                }
                if (own_database) {
                    ++database_aliases;
                    return;
                }
                if (file.size_bytes >
                    static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)())) {
                    throw StorageError("File size exceeds supported database range", 1);
                }
                const auto key = utf8(file.path.lexically_normal());
                const auto modified = modified_nanoseconds(file.modified_at);
                const auto created = creation_nanoseconds(file.path);
                const auto size = static_cast<std::int64_t>(file.size_bytes);
                bind_text(seen.get(), 1, key);
                finish(seen.get());
                bind_integer(existing.get(), 1, root_id);
                bind_text(existing.get(), 2, key);
                const int found = sqlite3_step(existing.get());
                if (found != SQLITE_ROW && found != SQLITE_DONE) {
                    throw StorageError("Cannot read existing file metadata", 1);
                }
                const bool changed =
                    found == SQLITE_DONE || sqlite3_column_int64(existing.get(), 0) != size ||
                    sqlite3_column_int64(existing.get(), 1) != modified ||
                    sqlite3_column_int(existing.get(), 2) == 0 ||
                    (created && (sqlite3_column_type(existing.get(), 3) == SQLITE_NULL ||
                                 sqlite3_column_int64(existing.get(), 3) != *created));
                if (sqlite3_reset(existing.get()) != SQLITE_OK ||
                    sqlite3_clear_bindings(existing.get()) != SQLITE_OK) {
                    throw StorageError("Cannot reset metadata reader", 1);
                }
                if (!changed) {
                    return;
                }
                if (found == SQLITE_DONE) {
                    ++summary.added;
                } else {
                    ++summary.updated;
                }
                bind_integer(write.get(), 1, root_id);
                bind_text(write.get(), 2, key);
                bind_text(write.get(), 3, utf8(file.path));
                bind_text(write.get(), 4, utf8(file.path.filename()));
                bind_text(write.get(), 5, utf8(file.path.extension()));
                bind_integer(write.get(), 6, size);
                bind_integer(write.get(), 7, modified);
                bind_integer(write.get(), 8, started);
                if (created) {
                    bind_integer(write.get(), 9, *created);
                } else if (sqlite3_bind_null(write.get(), 9) != SQLITE_OK) {
                    throw StorageError("Cannot bind unknown creation time", 1);
                }
                finish(write.get());
            },
            stop);
        summary.scan.files -= database_aliases;
        summary.scan.skipped += database_aliases;
        if (!summary.scan.complete) {
            execute(db.get(), "ROLLBACK; BEGIN IMMEDIATE");
            summary.added = 0;
            summary.updated = 0;
            record_status(summary.scan.cancelled ? "cancelled" : "incomplete");
        } else {
            if (summary.scan.skipped == 0 && request.scan.excluded_paths.empty()) {
                auto missing =
                    prepare(db.get(),
                            "UPDATE files SET is_present=0,sha256=NULL,hash_metadata_version=NULL "
                            "WHERE root_id=?1 AND is_present=1 AND normalized_path NOT IN (SELECT "
                            "path FROM seen)");
                bind_integer(missing.get(), 1, root_id);
                finish(missing.get());
                summary.missing = sqlite3_changes(db.get());
                summary.missing_reconciled = true;
            }
            record_status("completed");
        }
        execute(db.get(), "COMMIT");
    } catch (...) {
        sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        summary.added = 0;
        summary.updated = 0;
        summary.missing = 0;
        execute(db.get(), "BEGIN IMMEDIATE");
        try {
            record_status("failed");
            execute(db.get(), "COMMIT");
        } catch (...) {
            sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
        throw;
    }
    return summary;
}
RefreshSummary refresh_index(const std::filesystem::path &database,
                             const std::function<std::int64_t()> &clock,
                             const std::stop_token &stop) {
    if (!clock) {
        throw std::invalid_argument("A refresh clock is required");
    }
    if (database.empty() || utf8(database).find('\0') != std::string::npos) {
        throw StorageError("An explicit database path is required", 2);
    }
    struct Plan {
        RefreshRootResult result;
        std::optional<ScanOptions> options;
    };
    std::vector<Plan> plans;
    {
        std::error_code error;
        const bool present = std::filesystem::exists(database, error);
        if (error) {
            throw StorageError("Cannot inspect refresh database", 3);
        }
        if (!present) {
            throw StorageError("Index is not initialized; run init --database first", 4);
        }
        sqlite3 *raw = nullptr;
        const int opened =
            sqlite3_open_v2(utf8(database).c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
        Connection db(raw, sqlite3_close);
        if (opened != SQLITE_OK) {
            throw StorageError("Cannot open refresh database", 3);
        }
        if (sqlite3_busy_timeout(db.get(), 5000) != SQLITE_OK) {
            throw StorageError("Cannot configure refresh timeout", 1);
        }
        execute(db.get(), "BEGIN");
        if (scalar(db.get(), "PRAGMA user_version") != 3) {
            throw StorageError(
                "Refresh requires schema v3; run init, then re-index legacy roots to save policies",
                5);
        }
        auto roots =
            prepare(db.get(), "SELECT roots.id,normalized_path,maximum_depth FROM roots "
                              "LEFT JOIN root_scan_options ON roots.id=root_scan_options.root_id "
                              "ORDER BY normalized_path COLLATE BINARY,roots.id LIMIT 10001");
        std::size_t text_bytes = 0;
        std::size_t path_count = 0;
        const auto read_path = [&](sqlite3_stmt *row, int column) {
            if (sqlite3_column_type(row, column) != SQLITE_TEXT) {
                throw StorageError("Invalid stored root or exclusion path", 5);
            }
            const int length = sqlite3_column_bytes(row, column);
            const auto *text = sqlite3_column_text(row, column);
            if (sqlite3_column_type(row, column) != SQLITE_TEXT || text == nullptr || length <= 0 ||
                length > 131072) {
                throw StorageError("Invalid stored root or exclusion path", 5);
            }
            text_bytes += static_cast<std::size_t>(length);
            if (text_bytes > std::size_t{16} * 1024 * 1024) {
                throw StorageError("Refresh plan exceeds 16 MiB text budget", 1);
            }
            const std::string bytes(reinterpret_cast<const char *>(text),
                                    static_cast<std::size_t>(length));
            return std::filesystem::path(std::u8string(bytes.begin(), bytes.end()));
        };
        for (;;) {
            const int step = sqlite3_step(roots.get());
            if (step == SQLITE_DONE) {
                break;
            }
            if (step != SQLITE_ROW || sqlite3_column_type(roots.get(), 0) != SQLITE_INTEGER) {
                throw StorageError("Cannot read registered roots", 5);
            }
            if (plans.size() == 10000) {
                throw StorageError("Refresh supports at most 10000 registered roots", 1);
            }
            Plan plan;
            plan.result.root_id = sqlite3_column_int64(roots.get(), 0);
            plan.result.root = read_path(roots.get(), 1);
            if (sqlite3_column_type(roots.get(), 2) != SQLITE_NULL) {
                const auto depth = sqlite3_column_int64(roots.get(), 2);
                if (sqlite3_column_type(roots.get(), 2) != SQLITE_INTEGER || depth < 1 ||
                    depth > 128) {
                    throw StorageError("Invalid saved scan depth", 5);
                }
                plan.options.emplace();
                plan.options->maximum_depth = static_cast<std::size_t>(depth);
                auto exclusions = prepare(db.get(), "SELECT path FROM root_exclusions WHERE "
                                                    "root_id=?1 ORDER BY ordinal LIMIT 10001");
                bind_integer(exclusions.get(), 1, plan.result.root_id);
                for (;;) {
                    const int path_step = sqlite3_step(exclusions.get());
                    if (path_step == SQLITE_DONE) {
                        break;
                    }
                    if (path_step != SQLITE_ROW) {
                        throw StorageError("Cannot read registered exclusions", 5);
                    }
                    if (++path_count > 10000) {
                        throw StorageError("Refresh supports at most 10000 saved exclusions", 1);
                    }
                    plan.options->excluded_paths.push_back(read_path(exclusions.get(), 0));
                }
            }
            plans.push_back(std::move(plan));
        }
        execute(db.get(), "COMMIT");
    }
    RefreshSummary summary;
    summary.cancelled = stop.stop_requested();
    summary.roots.reserve(plans.size());
    for (auto &plan : plans) {
        auto &result = plan.result;
        if (summary.cancelled || stop.stop_requested()) {
            summary.cancelled = true;
        } else if (!plan.options) {
            result.exit_code = 5;
            result.error = "Saved scan policy is unknown; run index with explicit exclusions first";
        } else {
            result.attempted = true;
            try {
                result.index = index_directory(
                    {database, result.root, *plan.options, result.root_id}, clock, stop);
                result.exit_code = result.index->scan.complete ? 0 : 1;
                result.error =
                    result.exit_code == 0 ? "" : "Scan incomplete; prior metadata retained";
                if (result.index->scan.cancelled) {
                    summary.cancelled = true;
                }
            } catch (const StorageError &failure) {
                result.exit_code = failure.exit_code();
                result.error = failure.what();
            } catch (const std::exception &failure) {
                result.exit_code = 1;
                result.error = failure.what();
            }
        }
        summary.roots.push_back(std::move(result));
    }
    summary.complete =
        !summary.cancelled && std::all_of(summary.roots.begin(), summary.roots.end(),
                                          [](const auto &result) { return result.exit_code == 0; });
    return summary;
}

std::vector<RootInfo> list_roots(const std::filesystem::path &database,
                                 const std::stop_token &stop) {
    auto cancellation = stop;
    auto db = open_inspection(database, cancellation);
    const int version = scalar(db.get(), "PRAGMA user_version");
    auto roots =
        prepare(db.get(),
                version == 3
                    ? "SELECT roots.id,normalized_path,last_scan_status,last_scan_at,maximum_depth "
                      "FROM roots "
                      "LEFT JOIN root_scan_options ON roots.id=root_scan_options.root_id ORDER BY "
                      "normalized_path COLLATE BINARY,roots.id LIMIT 10001"
                    : "SELECT id,normalized_path,last_scan_status,last_scan_at,NULL FROM roots "
                      "ORDER BY normalized_path COLLATE BINARY,id LIMIT 10001");
    std::vector<RootInfo> result;
    std::size_t bytes = 0;
    std::size_t paths = 0;
    while (inspection_row(roots.get(), stop)) {
        if (result.size() == 10000) {
            throw StorageError("Root inventory exceeds 10000 roots", 1);
        }
        RootInfo root;
        root.root_id = inspection_integer(roots.get(), 0);
        root.path = inspection_text(roots.get(), 1, bytes);
        root.last_scan_status = inspection_text(roots.get(), 2, bytes);
        if (root.path.empty() || root.path.find('\0') != std::string::npos ||
            (root.last_scan_status != "never" && root.last_scan_status != "running" &&
             root.last_scan_status != "completed" && root.last_scan_status != "incomplete" &&
             root.last_scan_status != "cancelled" && root.last_scan_status != "failed")) {
            throw StorageError("Invalid stored root metadata", 5);
        }
        if (sqlite3_column_type(roots.get(), 3) != SQLITE_NULL) {
            root.last_scan_at = inspection_integer(roots.get(), 3);
        }
        if (sqlite3_column_type(roots.get(), 4) != SQLITE_NULL) {
            root.maximum_depth = inspection_integer(roots.get(), 4);
            if (*root.maximum_depth < 1 || *root.maximum_depth > 128) {
                throw StorageError("Invalid saved scan depth", 5);
            }
            root.policy_known = true;
            auto exclusions = prepare(
                db.get(),
                "SELECT path FROM root_exclusions WHERE root_id=?1 ORDER BY ordinal LIMIT 10001");
            bind_integer(exclusions.get(), 1, root.root_id);
            while (inspection_row(exclusions.get(), stop)) {
                if (++paths > 10000) {
                    throw StorageError("Root inventory exceeds 10000 exclusions", 1);
                }
                auto path = inspection_text(exclusions.get(), 0, bytes);
                if (path.empty() || path.find('\0') != std::string::npos) {
                    throw StorageError("Invalid saved exclusion", 5);
                }
                root.exclusions.push_back(std::move(path));
            }
        }
        result.push_back(std::move(root));
    }
    if (stop.stop_requested()) {
        throw StorageError("Inspection cancelled", 1);
    }
    return result;
}

CatalogStatus catalog_status(const std::filesystem::path &database, const std::stop_token &stop) {
    auto cancellation = stop;
    auto db = open_inspection(database, cancellation);
    CatalogStatus result;
    result.schema_version = scalar(db.get(), "PRAGMA user_version");
    auto counts =
        prepare(db.get(),
                "SELECT (SELECT count(*) FROM roots),"
                "(SELECT count(*) FROM files WHERE is_present=1),(SELECT count(*) FROM files WHERE "
                "is_present=0),"
                "(SELECT count(*) FROM scans),(SELECT count(*) FROM scans WHERE status='running')");
    if (!inspection_row(counts.get(), stop)) {
        throw StorageError("Missing catalog counts", 5);
    }
    result.roots = inspection_integer(counts.get(), 0);
    result.present_files = inspection_integer(counts.get(), 1);
    result.missing_files = inspection_integer(counts.get(), 2);
    result.scan_records = inspection_integer(counts.get(), 3);
    result.running_scan_records = inspection_integer(counts.get(), 4);
    if (stop.stop_requested()) {
        throw StorageError("Inspection cancelled", 1);
    }
    return result;
}

std::vector<SearchResult> search_files(const SearchRequest &request, const std::stop_token &stop) {
    if (request.sort != SearchSort::name && request.sort != SearchSort::size &&
        request.sort != SearchSort::modified && request.sort != SearchSort::relevance) {
        throw StorageError("Unsupported search sort", 2);
    }
    if (request.query.empty() || request.query.size() > 4096 ||
        request.query.find('\0') != std::string::npos || request.limit == 0 ||
        request.limit > 10000 ||
        (request.mode != SearchMode::exact && request.mode != SearchMode::substring &&
         request.mode != SearchMode::prefix && request.mode != SearchMode::fuzzy)) {
        throw StorageError("Search requires a nonempty query up to 4096 bytes and limit 1..10000",
                           2);
    }
    if (request.database.empty() || utf8(request.database).find('\0') != std::string::npos) {
        throw StorageError("An explicit database path without null characters is required", 2);
    }
    const bool fuzzy = request.mode == SearchMode::fuzzy;
    if ((request.sort == SearchSort::relevance) != fuzzy || (fuzzy && request.descending)) {
        throw StorageError(
            "Fuzzy mode requires relevance sort without descending; relevance is fuzzy-only", 2);
    }
    std::optional<FuzzyMatcher> matcher;
    if (fuzzy) {
        if (request.fuzzy.candidate_limit == 0 || request.fuzzy.candidate_limit > 10000) {
            throw StorageError("Fuzzy candidate limit requires 1..10000", 2);
        }
        matcher.emplace(request.query, request.fuzzy.weights);
    }
    const auto &filters = request.filters;
    if (filters.modified_after && filters.modified_before &&
        *filters.modified_after >= *filters.modified_before) {
        throw StorageError("Modified-after must be earlier than modified-before", 2);
    }
    if ((filters.minimum_size && *filters.minimum_size < 0) ||
        (filters.maximum_size && *filters.maximum_size < 0) ||
        (filters.minimum_size && filters.maximum_size &&
         *filters.minimum_size > *filters.maximum_size)) {
        throw StorageError("Size filters require nonnegative bytes with minimum <= maximum", 2);
    }
    auto extension = filters.extension;
    if (extension) {
        if (extension->size() > 4096 || extension->find('\0') != std::string::npos ||
            extension->find_first_of("/\\") != std::string::npos) {
            throw StorageError("Invalid extension filter", 2);
        }
        if (!extension->empty() && extension->front() != '.') {
            extension->insert(extension->begin(), '.');
        }
    }
    std::optional<std::string> root_key;
    if (filters.root) {
        if (filters.root->empty() || utf8(*filters.root).find('\0') != std::string::npos) {
            throw StorageError("Root filter requires a nonempty path without null characters", 2);
        }
        std::error_code root_error;
        auto absolute = std::filesystem::absolute(*filters.root, root_error).lexically_normal();
        if (root_error) {
            throw StorageError("Cannot resolve root filter", 2);
        }
        while (!absolute.has_filename() && absolute != absolute.root_path()) {
            absolute = absolute.parent_path();
        }
        root_key = utf8(absolute);
    }
    if (stop.stop_requested()) {
        throw StorageError("Search cancelled", 1);
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(request.database, error);
    if (error) {
        throw StorageError("Cannot inspect search database", 3);
    }
    if (!exists) {
        throw StorageError("Index is not initialized; run init --database first", 4);
    }
    auto cancellation = stop;
    sqlite3 *raw = nullptr;
    const int opened =
        sqlite3_open_v2(utf8(request.database).c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    Connection db(raw, sqlite3_close);
    if (opened != SQLITE_OK) {
        throw StorageError("Cannot open search database", 3);
    }
    if (sqlite3_busy_timeout(db.get(), 5000) != SQLITE_OK) {
        throw StorageError("Cannot configure search timeout", 1);
    }
    sqlite3_progress_handler(
        db.get(), 1000,
        [](void *context) {
            return static_cast<std::stop_token *>(context)->stop_requested() ? 1 : 0;
        },
        &cancellation);
    execute(db.get(), "BEGIN");
    const int search_version = scalar(db.get(), "PRAGMA user_version");
    if (search_version != 2 && search_version != 3) {
        throw StorageError("Search requires schema v2 or v3; run init --database to upgrade", 5);
    }
    // Only internal constant SQL fragments are selected; every user value is bound.
    const char *primary = request.sort == SearchSort::size       ? "size_bytes"
                          : request.sort == SearchSort::modified ? "modified_at"
                                                                 : "name COLLATE BINARY";
    const std::string ordering =
        std::string("ORDER BY ") + primary + (request.descending ? " DESC," : " ASC,") +
        "name COLLATE BINARY ASC,normalized_path COLLATE BINARY ASC,root_id ASC,id ASC LIMIT ?2";
    const char *predicate = fuzzy                               ? "?1 IS NOT NULL "
                            : request.mode == SearchMode::exact ? "name=?1 "
                            : request.mode == SearchMode::prefix
                                ? "instr(lower(name),lower(?1))=1 "
                                : "instr(lower(name),lower(?1))>0 ";
    const std::string sql =
        std::string(
            "SELECT id,root_id,display_path,name,size_bytes FROM files WHERE is_present=1 AND ") +
        predicate +
        "AND (?3 IS NULL OR lower(extension)=lower(?3)) "
        "AND (?4 IS NULL OR size_bytes>=?4) AND (?5 IS NULL OR size_bytes<=?5) "
        "AND (?6 IS NULL OR root_id IN (SELECT id FROM roots WHERE normalized_path=?6)) "
        "AND (?7 IS NULL OR modified_at>?7) AND (?8 IS NULL OR modified_at<?8) " +
        ordering;
    auto query = prepare(db.get(), sql.c_str());
    bind_text(query.get(), 1, request.query);
    bind_integer(query.get(), 2, fuzzy ? -1 : static_cast<std::int64_t>(request.limit));
    const auto bind_null = [&](int position) {
        if (sqlite3_bind_null(query.get(), position) != SQLITE_OK) {
            throw StorageError("Cannot bind optional search filter", 1);
        }
    };
    if (extension) {
        bind_text(query.get(), 3, *extension);
    } else {
        bind_null(3);
    }
    if (filters.minimum_size) {
        bind_integer(query.get(), 4, *filters.minimum_size);
    } else {
        bind_null(4);
    }
    if (filters.maximum_size) {
        bind_integer(query.get(), 5, *filters.maximum_size);
    } else {
        bind_null(5);
    }
    if (root_key) {
        bind_text(query.get(), 6, *root_key);
    } else {
        bind_null(6);
    }
    if (filters.modified_after) {
        bind_integer(query.get(), 7, *filters.modified_after);
    } else {
        bind_null(7);
    }
    if (filters.modified_before) {
        bind_integer(query.get(), 8, *filters.modified_before);
    } else {
        bind_null(8);
    }
    std::vector<SearchResult> results;
    results.reserve(request.limit);
    std::size_t text_bytes = 0;
    std::size_t candidates = 0;
    std::vector<std::pair<std::int64_t, SearchResult>> ranked;
    const auto text_column = [&](int column) {
        if (sqlite3_column_type(query.get(), column) != SQLITE_TEXT) {
            throw StorageError("Invalid indexed text", 5);
        }
        const auto *value = sqlite3_column_text(query.get(), column);
        const int length = sqlite3_column_bytes(query.get(), column);
        if (value == nullptr || length < 0 || length > 131072) {
            throw StorageError("Invalid indexed text", 5);
        }
        return std::string(reinterpret_cast<const char *>(value), static_cast<std::size_t>(length));
    };
    for (;;) {
        if (stop.stop_requested()) {
            throw StorageError("Search cancelled", 1);
        }
        const int step = sqlite3_step(query.get());
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            throw StorageError(
                step == SQLITE_INTERRUPT ? "Search cancelled" : "Cannot execute search", 1);
        }
        if (sqlite3_column_type(query.get(), 0) != SQLITE_INTEGER ||
            sqlite3_column_type(query.get(), 1) != SQLITE_INTEGER ||
            sqlite3_column_type(query.get(), 4) != SQLITE_INTEGER ||
            sqlite3_column_int64(query.get(), 4) < 0) {
            throw StorageError("Invalid indexed metadata", 5);
        }
        auto name = text_column(3);
        if (matcher && !matcher->candidate(name, stop)) {
            continue;
        }
        if (matcher && ++candidates > request.fuzzy.candidate_limit) {
            throw StorageError("Fuzzy candidate budget exceeded; narrow the query or filters, or "
                               "raise --fuzzy-candidates (max 10000)",
                               1);
        }
        auto path = text_column(2);
        text_bytes += name.size() + path.size();
        if (text_bytes > std::size_t{16} * 1024 * 1024) {
            throw StorageError("Search result metadata exceeds the 16 MiB text budget; narrow "
                               "query or lower limit",
                               1);
        }
        SearchResult result{sqlite3_column_int64(query.get(), 0),
                            sqlite3_column_int64(query.get(), 1), std::move(path), std::move(name),
                            sqlite3_column_int64(query.get(), 4)};
        if (matcher) {
            const auto score = matcher->score(result.name, stop);
            if (score) {
                ranked.emplace_back(*score, std::move(result));
            }
        } else {
            results.push_back(std::move(result));
        }
    }
    if (matcher) {
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
            return left.first > right.first;
        });
        for (auto &entry : ranked) {
            if (results.size() == request.limit) {
                break;
            }
            results.push_back(std::move(entry.second));
        }
    }
    if (stop.stop_requested()) {
        throw StorageError("Search cancelled", 1);
    }
    execute(db.get(), "COMMIT");
    return results;
}
} // namespace fileweaver
