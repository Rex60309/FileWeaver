#include "../support/directory_link.hpp"
#include "fileweaver/application/index.hpp"
#include "fileweaver/application/initialize.hpp"
#include "fileweaver/application/inspect.hpp"
#include "fileweaver/application/refresh.hpp"
#include "fileweaver/application/search.hpp"
#include "fileweaver/core/glob.hpp"
#include "fileweaver/core/storage_error.hpp"
#include "fileweaver/core/utc_time.hpp"
#include "fileweaver/core/watch_schedule.hpp"
#include "fileweaver/filesystem/scan.hpp"
#include "output.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <sqlite3.h>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <array>
#include <windows.h>
#include <winioctl.h>
#endif

namespace {
class Fixture {
  public:
    Fixture() {
        std::random_device random;
        for (int attempt = 0; attempt < 20; ++attempt) {
            directory = std::filesystem::temp_directory_path() /
                        ("fileweaver-test-" + std::to_string(random()));
            if (std::filesystem::create_directory(directory)) {
                directory = std::filesystem::canonical(directory);
                return;
            }
        }
        throw std::runtime_error("Cannot create isolated fixture");
    }
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;
    std::filesystem::path directory;
    [[nodiscard]] std::filesystem::path path() const { return directory / "index.sqlite"; }
};

// Catch runs these cases serially in their own test process. Restore CWD even
// when an assertion throws, rather than depending on the runner's drive/layout.
class ScopedCurrentDirectory {
  public:
    explicit ScopedCurrentDirectory(const std::filesystem::path &selected)
        : previous(std::filesystem::current_path()) {
        std::filesystem::current_path(selected);
    }
    ~ScopedCurrentDirectory() {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        if (error) {
            std::terminate(); // Cannot safely continue tests with an unrestored environment.
        }
    }
    ScopedCurrentDirectory(const ScopedCurrentDirectory &) = delete;
    ScopedCurrentDirectory &operator=(const ScopedCurrentDirectory &) = delete;

  private:
    std::filesystem::path previous;
};

const fileweaver::IndexSummary &require_index(const fileweaver::RefreshRootResult &root) {
    if (!root.index) {
        throw std::runtime_error("Expected a per-root index summary");
    }
    return *root.index;
}

void sql(const std::filesystem::path &path, const char *query) {
    const auto bytes = path.u8string();
    const std::string name(bytes.begin(), bytes.end());
    sqlite3 *raw = nullptr;
    const int result = sqlite3_open(name.c_str(), &raw);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    REQUIRE(result == SQLITE_OK);
    REQUIRE(sqlite3_exec(db.get(), query, nullptr, nullptr, nullptr) == SQLITE_OK);
}

using fileweaver::test::DirectoryLink;
void directory_link(const DirectoryLink &paths) { fileweaver::test::directoryLink(paths); }
using CancellationRow =
    std::tuple<std::int64_t, std::int64_t, std::string, std::string, std::int64_t>;

std::vector<CancellationRow> cancellation_snapshot(const std::filesystem::path &database) {
    std::vector<CancellationRow> snapshot;
    for (const auto &row : fileweaver::search_files(
             {database, "cancel-", fileweaver::SearchMode::substring, 10000})) {
        snapshot.emplace_back(row.file_id, row.root_id, row.name, row.display_path, row.size_bytes);
    }
    return snapshot;
}

void write_cancellation_file(const std::filesystem::path &path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.is_open());
    stream << contents;
    stream.flush();
    REQUIRE(stream.good());
}

std::vector<std::filesystem::path> seed_cancellation_roots(Fixture &fixture) {
    const std::vector<std::filesystem::path> roots{
        fixture.directory / std::filesystem::path(u8"\u8CC7\u6599 a-root"),
        fixture.directory / std::filesystem::path(u8"\u8CC7\u6599 b-root")};
    write_cancellation_file(roots[0] / "cancel-a.txt", "old");
    write_cancellation_file(roots[1] / "cancel-b.txt", "old");
    fileweaver::initialize_database(fixture.path());
    for (const auto &root : roots) {
        REQUIRE(fileweaver::index_directory({fixture.path(), root, {}}, [] {
                    return std::int64_t{1};
                }).added == 1);
    }
    return roots;
}

void require_cancelled_before_delivery(const fileweaver::IndexSummary &summary) {
    REQUIRE(summary.scan.cancelled);
    REQUIRE_FALSE(summary.scan.complete);
    REQUIRE(summary.scan.files == 0);
    REQUIRE(summary.scan.errors == 0);
    REQUIRE(summary.scan.skipped == 0);
    REQUIRE(summary.added == 0);
    REQUIRE(summary.updated == 0);
    REQUIRE(summary.missing == 0);
    REQUIRE_FALSE(summary.missing_reconciled);
}
} // namespace

TEST_CASE("Fresh and repeated initialization preserve settings") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(), "INSERT INTO settings VALUES('retained','yes')");
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(), "CREATE TEMP TABLE assertion(value INTEGER CHECK(value=1));"
                        "INSERT INTO assertion SELECT count(*) FROM settings WHERE key='retained'");
}

TEST_CASE("Unsupported versions and unrelated databases are rejected") {
    Fixture fixture;
    sql(fixture.path(), "PRAGMA user_version=99");
    REQUIRE_THROWS_AS(fileweaver::initialize_database(fixture.path()), fileweaver::StorageError);
    sql(fixture.path(), "PRAGMA user_version=0; CREATE TABLE unrelated(value TEXT)");
    REQUIRE_THROWS_AS(fileweaver::initialize_database(fixture.path()), fileweaver::StorageError);
    sql(fixture.path(), "DROP TABLE unrelated");
    fileweaver::initialize_database(fixture.path());
}

TEST_CASE("Migration failure rolls back schema and version") {
    Fixture fixture;
    sql(fixture.path(), "CREATE VIEW settings AS SELECT 1 AS unexpected");
    REQUIRE_THROWS_AS(fileweaver::initialize_database(fixture.path()), fileweaver::StorageError);
    sql(fixture.path(), "CREATE TEMP TABLE assertion(value INTEGER CHECK(value=0));"
                        "INSERT INTO assertion SELECT user_version FROM pragma_user_version;"
                        "DROP VIEW settings");
    fileweaver::initialize_database(fixture.path());
}

TEST_CASE("Missing parent, empty path and corrupt databases fail") {
    Fixture fixture;
    REQUIRE_THROWS_AS(fileweaver::initialize_database(fixture.directory / "missing" / "db"),
                      fileweaver::StorageError);
    REQUIRE_THROWS_AS(fileweaver::initialize_database({}), fileweaver::StorageError);
    {
        std::ofstream stream(fixture.path());
        stream << "not a database";
    }
    REQUIRE_THROWS_AS(fileweaver::initialize_database(fixture.path()), fileweaver::StorageError);
}

TEST_CASE("Unicode paths are supported") {
    Fixture fixture;
    const auto path = fixture.directory / std::filesystem::path(u8"資料 index.sqlite");
    fileweaver::initialize_database(path);
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE("Version one with incompatible columns is rejected") {
    Fixture fixture;
    sql(fixture.path(), "CREATE TABLE settings(key INTEGER, value TEXT); PRAGMA user_version=1");
    REQUIRE_THROWS_AS(fileweaver::initialize_database(fixture.path()), fileweaver::StorageError);
}

TEST_CASE("Initialization enables persistent WAL mode") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(), "CREATE TEMP TABLE assertion(value TEXT CHECK(value='wal'));"
                        "INSERT INTO assertion SELECT journal_mode FROM pragma_journal_mode");
}

TEST_CASE("Scanner streams only regular file metadata from an explicit Unicode root") {
    Fixture fixture;
    const auto root = fixture.directory / std::filesystem::path(u8"資料 space");
    std::filesystem::create_directories(root / "nested");
    {
        std::ofstream file(root / "empty.txt");
    }
    {
        std::ofstream file(root / "nested" / "data.bin", std::ios::binary);
        file << "abc";
    }
    {
        std::ofstream file(fixture.directory / "outside.txt");
        file << "outside";
    }
    std::vector<fileweaver::FileMetadata> records;
    const auto result =
        fileweaver::scan_directory(root, {}, [&](const auto &file) { records.push_back(file); });
    REQUIRE(result.files == 2);
    REQUIRE(result.errors == 0);
    REQUIRE(result.complete);
    REQUIRE_FALSE(result.cancelled);
    REQUIRE(records.size() == 2);
    std::uintmax_t total_size = 0;
    for (const auto &record : records) {
        REQUIRE(record.path.is_absolute());
        REQUIRE(record.modified_at == std::filesystem::last_write_time(record.path));
        total_size += record.size_bytes;
    }
    REQUIRE(total_size == 3);
}

TEST_CASE("Empty roots complete and invalid roots fail without invoking the sink") {
    Fixture fixture;
    unsigned calls = 0;
    const auto sink = [&](const auto &) { ++calls; };
    const auto empty = fileweaver::scan_directory(fixture.directory, {}, sink);
    REQUIRE(empty.files == 0);
    REQUIRE(empty.complete);
    REQUIRE_THROWS_AS(fileweaver::scan_directory({}, {}, sink), fileweaver::StorageError);
    REQUIRE_THROWS_AS(fileweaver::scan_directory(fixture.directory / "missing", {}, sink),
                      fileweaver::StorageError);
    {
        std::ofstream file(fixture.path());
    }
    REQUIRE_THROWS_AS(fileweaver::scan_directory(fixture.path(), {}, sink),
                      fileweaver::StorageError);
    REQUIRE_THROWS_AS(fileweaver::scan_directory(fixture.directory, {}, {}), std::invalid_argument);
    REQUIRE(calls == 0);
}

TEST_CASE("Explicit exclusions prune files, subtrees and selected roots") {
    Fixture fixture;
    std::filesystem::create_directory(fixture.directory / "cache");
    {
        std::ofstream file(fixture.directory / "cache" / "hidden.txt");
    }
    {
        std::ofstream file(fixture.directory / "keep.txt");
    }
    {
        std::ofstream file(fixture.path());
    }
    fileweaver::ScanOptions options{{"cache", fixture.path()}};
    unsigned calls = 0;
    const auto sink = [&](const auto &file) {
        REQUIRE(file.path.filename() == "keep.txt");
        ++calls;
    };
    const auto result = fileweaver::scan_directory(fixture.directory, options, sink);
    REQUIRE(result.files == 1);
    REQUIRE(result.skipped == 2);
    REQUIRE(result.complete);
    options.excluded_paths = {fixture.directory};
    const auto excluded = fileweaver::scan_directory(fixture.directory, options, sink);
    REQUIRE(excluded.files == 0);
    REQUIRE(excluded.skipped == 1);
    REQUIRE(calls == 1);
}

TEST_CASE("Cancellation before and during scanning is reported as incomplete") {
    Fixture fixture;
    {
        std::ofstream file(fixture.directory / "one.txt");
    }
    {
        std::ofstream file(fixture.directory / "two.txt");
    }
    std::stop_source cancellation;
    const auto result = fileweaver::scan_directory(
        fixture.directory, {}, [&](const auto &) { cancellation.request_stop(); },
        cancellation.get_token());
    REQUIRE(result.files == 1);
    REQUIRE(result.cancelled);
    REQUIRE_FALSE(result.complete);
    unsigned calls = 0;
    const auto cancelled = fileweaver::scan_directory(
        fixture.directory, {}, [&](const auto &) { ++calls; }, cancellation.get_token());
    REQUIRE(cancelled.files == 0);
    REQUIRE(cancelled.cancelled);
    REQUIRE_FALSE(cancelled.complete);
    REQUIRE(calls == 0);
}

TEST_CASE("Sink failures propagate and release traversal resources") {
    Fixture fixture;
    {
        std::ofstream file(fixture.path());
    }
    REQUIRE_THROWS_AS(fileweaver::scan_directory(
                          fixture.directory, {},
                          [](const auto &) { throw std::runtime_error("consumer failure"); }),
                      std::runtime_error);
    REQUIRE(std::filesystem::remove(fixture.path()));
}

TEST_CASE("Files disappearing during a scan do not abort the root") {
    Fixture fixture;
    for (int index = 0; index < 5; ++index) {
        std::ofstream file(fixture.directory / std::to_string(index));
    }
    bool removed = false;
    const auto result = fileweaver::scan_directory(fixture.directory, {}, [&](const auto &current) {
        if (!removed) {
            removed = true;
            for (const auto &entry : std::filesystem::directory_iterator(fixture.directory)) {
                if (entry.path() != current.path) {
                    std::filesystem::remove(entry.path());
                }
            }
        }
    });
    REQUIRE(removed);
    REQUIRE(result.files == 1);
    if (result.errors != 0) {
        REQUIRE_FALSE(result.complete);
    }
}

TEST_CASE("Directory links are skipped and cannot be selected as roots") {
    Fixture fixture;
    const auto outside = fixture.directory / "outside";
    const auto root = fixture.directory / "selected";
    std::filesystem::create_directory(outside);
    std::filesystem::create_directory(root);
    std::filesystem::create_directory(outside / "child");
    {
        std::ofstream file(outside / "secret.txt");
    }
    const auto link = root / "linked";
    directory_link({outside, link});
    unsigned calls = 0;
    const auto sink = [&](const auto &) { ++calls; };
    const auto result = fileweaver::scan_directory(root, {}, sink);
    REQUIRE(result.files == 0);
    REQUIRE(result.skipped == 1);
    REQUIRE_THROWS_AS(fileweaver::scan_directory(link, {}, sink), fileweaver::StorageError);
    REQUIRE_THROWS_AS(fileweaver::scan_directory(link / "child", {}, sink),
                      fileweaver::StorageError);
    REQUIRE(calls == 0);
}

TEST_CASE("Depth is bounded and pruning is explicitly incomplete") {
    Fixture fixture;
    auto nested = fixture.directory;
    for (int depth = 0; depth < 5; ++depth) {
        nested /= "d";
        std::filesystem::create_directory(nested);
    }
    {
        std::ofstream file(fixture.directory / "visible");
    }
    {
        std::ofstream file(nested / "pruned");
    }
    fileweaver::ScanOptions options;
    options.maximum_depth = 3;
    const auto result = fileweaver::scan_directory(fixture.directory, options, [](const auto &) {});
    REQUIRE(result.files == 1);
    REQUIRE(result.errors == 1);
    REQUIRE(result.skipped == 1);
    REQUIRE_FALSE(result.complete);
}

TEST_CASE("Empty exclusions are rejected instead of implicitly excluding a root") {
    Fixture fixture;
    const fileweaver::ScanOptions options{{std::filesystem::path{}}};
    REQUIRE_THROWS_AS(fileweaver::scan_directory(fixture.directory, options, [](const auto &) {}),
                      std::invalid_argument);
}

TEST_CASE("Invalid depth limits are rejected") {
    Fixture fixture;
    fileweaver::ScanOptions options;
    for (const std::size_t limit : {std::size_t{0}, std::size_t{129}}) {
        options.maximum_depth = limit;
        REQUIRE_THROWS_AS(
            fileweaver::scan_directory(fixture.directory, options, [](const auto &) {}),
            std::invalid_argument);
    }
}

#ifdef _WIN32
TEST_CASE("Windows namespace roots fail before filesystem access") {
    for (const auto *path :
         {L"\\\\?\\C:\\", L"\\\\.\\C:\\", L"\\\\server\\share", L"//server/share", L"//?/C:/"}) {
        REQUIRE_THROWS_AS(fileweaver::scan_directory(path, {}, [](const auto &) {}),
                          fileweaver::StorageError);
    }
}
#endif

TEST_CASE("Schema v1 upgrades to current schema without losing settings") {
    Fixture fixture;
    sql(fixture.path(), "CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
                        "INSERT INTO settings VALUES('keep','original'); PRAGMA user_version=1");
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "CREATE TEMP TABLE version_assertion(v INTEGER CHECK(v=3));"
        "INSERT INTO version_assertion SELECT user_version FROM pragma_user_version;"
        "CREATE TEMP TABLE preserved(v TEXT CHECK(v='original'));"
        "INSERT INTO preserved SELECT value FROM settings WHERE key='keep';"
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/fixture','/fixture',0)");
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(), "CREATE TEMP TABLE retained(v INTEGER CHECK(v=1));"
                        "INSERT INTO retained SELECT count(*) FROM roots");
}

TEST_CASE("Conflicting v2 migration rolls back new tables and version") {
    Fixture fixture;
    sql(fixture.path(), "CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
                        "CREATE VIEW files AS SELECT 1 AS conflict; PRAGMA user_version=1");
    REQUIRE_THROWS_AS(fileweaver::initialize_database(fixture.path()), fileweaver::StorageError);
    sql(fixture.path(),
        "CREATE TEMP TABLE version_assertion(v INTEGER CHECK(v=1));"
        "INSERT INTO version_assertion SELECT user_version FROM pragma_user_version;"
        "CREATE TEMP TABLE absent(v INTEGER CHECK(v=0));"
        "INSERT INTO absent SELECT count(*) FROM sqlite_schema WHERE name IN ('roots','scans');"
        "DROP VIEW files");
    fileweaver::initialize_database(fixture.path());
}

TEST_CASE("Index schema enforces foreign keys and metadata constraints") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "PRAGMA foreign_keys=ON;"
        "CREATE TEMP TABLE assertion(v INTEGER CHECK(v=1));"
        "INSERT INTO assertion SELECT count(*) FROM pragma_foreign_key_list('files');"
        "INSERT INTO assertion SELECT count(*) FROM pragma_foreign_key_list('scans');"
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/fixture','/fixture',0)");
    const auto bytes = fixture.path().u8string();
    const std::string name(bytes.begin(), bytes.end());
    sqlite3 *raw = nullptr;
    REQUIRE(sqlite3_open(name.c_str(), &raw) == SQLITE_OK);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    REQUIRE(sqlite3_exec(db.get(), "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr) ==
            SQLITE_OK);
    for (const char *query :
         {"INSERT INTO scans(root_id,started_at,status) VALUES(999,0,'completed')",
          "INSERT INTO scans(root_id,started_at,status,scanned_count) VALUES(1,0,'completed',-1)",
          "INSERT INTO scans(root_id,started_at,status) VALUES(1,0,'unknown')",
          "INSERT INTO roots(normalized_path,display_path,added_at) "
          "VALUES('/fixture','duplicate',0)",
          "INSERT INTO "
          "files(root_id,normalized_path,display_path,name,extension,size_bytes,modified_at,"
          "indexed_at) VALUES(1,'a','a','a','',-1,0,0)"}) {
        REQUIRE(sqlite3_exec(db.get(), query, nullptr, nullptr, nullptr) == SQLITE_CONSTRAINT);
    }
}

TEST_CASE("Index persists metadata, skips unchanged updates and reconciles changes") {
    Fixture fixture;
    const auto root = fixture.directory / std::filesystem::path(u8"資料 root");
    std::filesystem::create_directory(root);
    {
        std::ofstream file(root / "quo'te.txt");
        file << "abc";
    }
    fileweaver::initialize_database(fixture.path());
    const fileweaver::IndexRequest request{fixture.path(), root, {}};
    std::int64_t now = 100;
    const auto clock = [&] { return now; };
    const auto first = fileweaver::index_directory(request, clock);
    REQUIRE(first.added == 1);
    REQUIRE(first.scan.complete);
#ifdef _WIN32
    constexpr auto creation_condition = "created_at IS NOT NULL";
#else
    constexpr auto creation_condition = "created_at IS NULL";
#endif
    const auto assertion =
        std::string("CREATE TEMP TABLE assertion(v INTEGER CHECK(v=1));"
                    "INSERT INTO assertion SELECT count(*) FROM files WHERE name='quo''te.txt' "
                    "AND size_bytes=3 AND indexed_at=100 AND ") +
        creation_condition;
    sql(fixture.path(), assertion.c_str());
    now = 200;
    const auto unchanged = fileweaver::index_directory(request, clock);
    REQUIRE(unchanged.added == 0);
    REQUIRE(unchanged.updated == 0);
    sql(fixture.path(), "CREATE TEMP TABLE assertion(v INTEGER CHECK(v=100));"
                        "INSERT INTO assertion SELECT indexed_at FROM files");
    {
        std::ofstream file(root / "quo'te.txt");
        file << "updated";
    }
    REQUIRE(fileweaver::index_directory(request, clock).updated == 1);
    std::filesystem::rename(root / "quo'te.txt", root / "renamed.txt");
    const auto renamed = fileweaver::index_directory(request, clock);
    REQUIRE(renamed.added == 1);
    REQUIRE(renamed.missing == 1);
    sql(fixture.path(), "CREATE TEMP TABLE assertion(v INTEGER CHECK(v=1));"
                        "INSERT INTO assertion SELECT count(*) FROM files WHERE is_present=1 AND "
                        "name='renamed.txt';"
                        "INSERT INTO assertion SELECT count(*) FROM files WHERE is_present=0 AND "
                        "name='quo''te.txt'");
    std::filesystem::remove(root / "renamed.txt");
    REQUIRE(fileweaver::index_directory(request, clock).missing == 1);
}

TEST_CASE("Incomplete and cancelled index scans preserve previous file metadata") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directories(root / "nested");
    {
        std::ofstream file(root / "top");
        file << "old";
    }
    {
        std::ofstream file(root / "nested" / "old");
    }
    fileweaver::initialize_database(fixture.path());
    fileweaver::IndexRequest request{fixture.path(), root, {}};
    const auto clock = [] { return std::int64_t{100}; };
    REQUIRE(fileweaver::index_directory(request, clock).added == 2);
    {
        std::ofstream file(root / "top");
        file << "changed bytes";
    }
    std::filesystem::remove(root / "nested" / "old");
    request.scan.maximum_depth = 1;
    const auto partial = fileweaver::index_directory(request, clock);
    REQUIRE_FALSE(partial.scan.complete);
    REQUIRE(partial.updated == 0);
    REQUIRE(partial.missing == 0);
    sql(fixture.path(),
        "CREATE TEMP TABLE assertion(v INTEGER CHECK(v=1));"
        "INSERT INTO assertion SELECT count(*) FROM files WHERE name='top' AND size_bytes=3;"
        "INSERT INTO assertion SELECT count(*) FROM files WHERE name='old' AND is_present=1;"
        "INSERT INTO assertion SELECT count(*) FROM scans WHERE status='incomplete'");
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = fileweaver::index_directory(request, clock, stop.get_token());
    REQUIRE(cancelled.scan.cancelled);
    sql(fixture.path(),
        "CREATE TEMP TABLE assertion(v INTEGER CHECK(v=1));"
        "INSERT INTO assertion SELECT count(*) FROM scans WHERE status='cancelled'");
}

TEST_CASE("Index automatically excludes its own database and retains excluded stale entries") {
    Fixture fixture;
    {
        std::ofstream file(fixture.directory / "keep");
    }
    fileweaver::initialize_database(fixture.path());
#ifdef _WIN32
    REQUIRE(CreateHardLinkW((fixture.directory / "database-alias").c_str(), fixture.path().c_str(),
                            nullptr) != FALSE);
#else
    std::filesystem::create_hard_link(fixture.path(), fixture.directory / "database-alias");
#endif
    fileweaver::IndexRequest request{fixture.path(), fixture.directory, {}};
    const auto clock = [] { return std::int64_t{0}; };
    REQUIRE(fileweaver::index_directory(request, clock).added == 1);
    sql(fixture.path(), "CREATE TEMP TABLE assertion(v INTEGER CHECK(v=0));"
                        "INSERT INTO assertion SELECT count(*) FROM files WHERE name LIKE "
                        "'%sqlite%' OR name='database-alias'");
    std::filesystem::remove(fixture.directory / "keep");
    request.scan.excluded_paths = {"keep"};
    REQUIRE(fileweaver::index_directory(request, clock).missing == 0);
    sql(fixture.path(),
        "CREATE TEMP TABLE assertion(v INTEGER CHECK(v=1));"
        "INSERT INTO assertion SELECT count(*) FROM files WHERE name='keep' AND is_present=1");
}

TEST_CASE("Index write failure rolls back metadata and records failure") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    {
        std::ofstream file(root / "data");
    }
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(), "CREATE TRIGGER reject_files BEFORE INSERT ON files BEGIN SELECT "
                        "RAISE(ABORT,'fixture failure'); END");
    const fileweaver::IndexRequest request{fixture.path(), root, {}};
    const auto clock = [] { return std::int64_t{0}; };
    REQUIRE_THROWS_AS(fileweaver::index_directory(request, clock), fileweaver::StorageError);
    sql(fixture.path(), "CREATE TEMP TABLE absent(v INTEGER CHECK(v=0));"
                        "INSERT INTO absent SELECT count(*) FROM files;"
                        "CREATE TEMP TABLE failed(v INTEGER CHECK(v=1));"
                        "INSERT INTO failed SELECT count(*) FROM scans WHERE status='failed';"
                        "DROP TRIGGER reject_files");
    REQUIRE(fileweaver::index_directory(request, clock).added == 1);
}

TEST_CASE("Index requires initialized storage and an explicit clock") {
    Fixture fixture;
    fileweaver::IndexRequest request{fixture.path(), fixture.directory, {}};
    REQUIRE_THROWS_AS(fileweaver::index_directory(request, [] { return std::int64_t{0}; }),
                      fileweaver::StorageError);
    REQUIRE_FALSE(std::filesystem::exists(fixture.path()));
    REQUIRE_THROWS_AS(fileweaver::index_directory(request, {}), std::invalid_argument);
}

TEST_CASE("Embedded nulls in scan paths are rejected before filesystem access") {
    Fixture fixture;
    auto native = fixture.directory.native();
    native.push_back(std::filesystem::path::value_type{});
    native += std::filesystem::path("suffix").native();
    const std::filesystem::path invalid(native);
    REQUIRE_THROWS_AS(fileweaver::scan_directory(invalid, {}, [](const auto &) {}),
                      fileweaver::StorageError);
    const fileweaver::ScanOptions options{{invalid}};
    REQUIRE_THROWS_AS(fileweaver::scan_directory(fixture.directory, options, [](const auto &) {}),
                      std::invalid_argument);
}

TEST_CASE("Absent excluded paths cannot be treated as missing evidence") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    {
        std::ofstream file(root / "excluded");
    }
    fileweaver::initialize_database(fixture.path());
    fileweaver::IndexRequest request{fixture.path(), root, {}};
    const auto clock = [] { return std::int64_t{0}; };
    REQUIRE(fileweaver::index_directory(request, clock).added == 1);
    std::filesystem::remove(root / "excluded");
    request.scan.excluded_paths = {"excluded"};
    REQUIRE(fileweaver::index_directory(request, clock).missing == 0);
}

TEST_CASE("Search exact, ASCII substring, Unicode and literal SQL characters") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directories(root / "a");
    std::filesystem::create_directories(root / "b");
    for (const auto &path :
         {root / "a" / "Report.txt", root / "b" / "report.txt", root / "100%_literal.txt",
          root / std::filesystem::path(u8"資料.txt")}) {
        std::ofstream file(path);
        file << "abc";
    }
    fileweaver::initialize_database(fixture.path());
    REQUIRE(fileweaver::index_directory({fixture.path(), root, {}}, [] {
                return std::int64_t{0};
            }).added == 4);
    fileweaver::SearchRequest request{fixture.path(), "Report.txt", fileweaver::SearchMode::exact,
                                      100};
    auto results = fileweaver::search_files(request);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].name == "Report.txt");
    REQUIRE(results[0].size_bytes == 3);
    request.query = "REPORT.TXT";
    REQUIRE(fileweaver::search_files(request).empty());
    request.mode = fileweaver::SearchMode::substring;
    REQUIRE(fileweaver::search_files(request).size() == 2);
    for (const auto *literal : {"%", "_"}) {
        request.query = literal;
        REQUIRE(fileweaver::search_files(request).size() == 1);
    }
    request.query = "' OR 1=1 --";
    REQUIRE(fileweaver::search_files(request).empty());
    const std::u8string unicode = u8"資料";
    request.query = std::string(unicode.begin(), unicode.end());
    REQUIRE(fileweaver::search_files(request).size() == 1);
}

TEST_CASE("Index records available native creation time and backfills without repeated writes",
          "[creation-time]") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    const auto path = root / std::filesystem::path(u8"\u8CC7\u6599\U0001F600.txt");
    {
        std::ofstream file(path);
        file << "abc";
    }
    fileweaver::initialize_database(fixture.path());
    const fileweaver::IndexRequest request{fixture.path(), root, {}};
    REQUIRE(fileweaver::index_directory(request, [] { return std::int64_t{1}; }).added == 1);
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    REQUIRE(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) != 0);
    const auto ticks =
        (static_cast<std::uint64_t>(attributes.ftCreationTime.dwHighDateTime) << 32U) |
        attributes.ftCreationTime.dwLowDateTime;
    constexpr std::uint64_t epoch = 116444736000000000ULL;
    REQUIRE(ticks >= epoch);
    REQUIRE((ticks - epoch) <=
            static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) / 100);
    const auto expected = static_cast<std::int64_t>((ticks - epoch) * 100);
    const auto assertion =
        "CREATE TEMP TABLE creation_assertion(v INTEGER CHECK(v=" + std::to_string(expected) +
        "));"
        "INSERT INTO creation_assertion SELECT created_at FROM files;"
        "CREATE TEMP TABLE not_null(v INTEGER NOT NULL);"
        "INSERT INTO not_null SELECT created_at FROM files;";
    sql(fixture.path(), assertion.c_str());
    sql(fixture.path(), "UPDATE files SET created_at=NULL");
    REQUIRE(fileweaver::index_directory(request, [] { return std::int64_t{2}; }).updated == 1);
    sql(fixture.path(), assertion.c_str());
#else
    sql(fixture.path(), "CREATE TEMP TABLE unknown(v INTEGER CHECK(v=1));"
                        "INSERT INTO unknown SELECT created_at IS NULL FROM files");
    REQUIRE(fileweaver::index_directory(request, [] { return std::int64_t{2}; }).updated == 0);
#endif
    const auto unchanged = fileweaver::index_directory(request, [] { return std::int64_t{3}; });
    REQUIRE(unchanged.updated == 0);
    REQUIRE(unchanged.added == 0);
    REQUIRE(unchanged.missing == 0);
    REQUIRE(fileweaver::catalog_status(fixture.path()).schema_version == 3);
}

#ifdef _WIN32
TEST_CASE("Windows creation ticks handle Unix epoch and signed nanosecond boundaries",
          "[creation-time]") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    constexpr std::uint64_t epoch = 116444736000000000ULL;
    constexpr auto maximum =
        static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) / 100;
    const std::array<std::uint64_t, 5> ticks{epoch, epoch - 1, epoch + maximum, epoch + maximum + 1,
                                             epoch - maximum - 1};
    for (std::size_t index = 0; index < ticks.size(); ++index) {
        const auto path = root / ("boundary-" + std::to_string(index) + ".txt");
        {
            std::ofstream file(path);
            file << "abc";
        }
        struct CloseFile {
            void operator()(void *handle) const noexcept { CloseHandle(handle); }
        };
        const auto raw = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        REQUIRE(raw != INVALID_HANDLE_VALUE);
        const std::unique_ptr<void, CloseFile> handle(raw);
        const FILETIME creation{static_cast<DWORD>(ticks[index] & 0xffffffffULL),
                                static_cast<DWORD>(ticks[index] >> 32U)};
        REQUIRE(SetFileTime(handle.get(), &creation, nullptr, nullptr) != 0);
    }
    fileweaver::initialize_database(fixture.path());
    const fileweaver::IndexRequest request{fixture.path(), root, {}};
    REQUIRE(fileweaver::index_directory(request, [] { return std::int64_t{0}; }).added == 5);
    sql(fixture.path(),
        "CREATE TEMP TABLE valid(v INTEGER NOT NULL CHECK(v=1));"
        "INSERT INTO valid SELECT created_at=0 FROM files WHERE name='boundary-0.txt';"
        "INSERT INTO valid SELECT created_at=-100 FROM files WHERE name='boundary-1.txt';"
        "INSERT INTO valid SELECT created_at=9223372036854775800 FROM files WHERE "
        "name='boundary-2.txt';"
        "INSERT INTO valid SELECT created_at IS NULL FROM files WHERE name IN "
        "('boundary-3.txt','boundary-4.txt');"
        "UPDATE files SET created_at=123 WHERE name='boundary-3.txt'");
    {
        std::ofstream file(root / "boundary-3.txt");
        file << "changed";
    }
    REQUIRE(fileweaver::index_directory(request, [] { return std::int64_t{1}; }).updated == 1);
    sql(fixture.path(),
        "CREATE TEMP TABLE preserved(v INTEGER NOT NULL CHECK(v=123));"
        "INSERT INTO preserved SELECT created_at FROM files WHERE name='boundary-3.txt'");
}
#endif

TEST_CASE("Prefix anchors literal filenames with ASCII-only folding", "[prefix-search]") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    const std::vector<std::u8string> names{
        u8"Report.txt",      u8"report-extra.md",     u8"xreport.txt",
        u8"%_report.txt",    u8"o'report.txt",        u8"\u5831\u544A_\u65E5\u672C\u8A9E.txt",
        u8"\u00E9clair.txt", u8"\U0001F600_start.txt"};
    for (const auto &name : names) {
        std::ofstream file(root / std::filesystem::path(name));
        file << "abc";
        REQUIRE(file.good());
    }
    fileweaver::initialize_database(fixture.path());
    REQUIRE(fileweaver::index_directory({fixture.path(), root, {}}, [] {
                return std::int64_t{0};
            }).added == 8);
    fileweaver::SearchRequest request{fixture.path(), "REP", fileweaver::SearchMode::prefix};
    auto found = fileweaver::search_files(request);
    REQUIRE(found.size() == 2);
    REQUIRE(found[0].name == "Report.txt");
    REQUIRE(found[1].name == "report-extra.md");
    for (const auto &query : {u8"%_", u8"o'", u8"\u5831\u544A", u8"\u00E9", u8"\U0001F600"}) {
        const std::u8string text(query);
        request.query.assign(text.begin(), text.end());
        REQUIRE(fileweaver::search_files(request).size() == 1);
    }
    for (const auto &query :
         {u8"_", u8"*", u8"' OR 1=1 --", u8"\u00C9", u8"\u65E5\u672C\u8A9E", u8"epo"}) {
        const std::u8string text(query);
        request.query.assign(text.begin(), text.end());
        REQUIRE(fileweaver::search_files(request).empty());
    }
    request.query = "report";
    request.mode = fileweaver::SearchMode::substring;
    REQUIRE(fileweaver::search_files(request).size() == 5);
    request.mode = fileweaver::SearchMode::exact;
    REQUIRE(fileweaver::search_files(request).empty());
}

TEST_CASE("Prefix reconciles Missing and preserves reappearing file identity", "[prefix-search]") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    const auto removed = root / "report-a.txt";
    {
        std::ofstream file(removed);
        file << "old";
    }
    {
        std::ofstream file(root / "report-b.txt");
        file << "keep";
    }
    fileweaver::initialize_database(fixture.path());
    const fileweaver::IndexRequest index{fixture.path(), root, {}};
    REQUIRE(fileweaver::index_directory(index, [] { return std::int64_t{0}; }).added == 2);
    const fileweaver::SearchRequest search{fixture.path(), "REPORT-",
                                           fileweaver::SearchMode::prefix};
    const auto initial = fileweaver::search_files(search);
    REQUIRE(initial.size() == 2);
    REQUIRE(initial[0].name == "report-a.txt");
    REQUIRE(std::filesystem::remove(removed));
    REQUIRE(fileweaver::search_files(search).size() == 2); // Metadata remains until reconciliation.
    const auto reconciled = fileweaver::index_directory(index, [] { return std::int64_t{1}; });
    REQUIRE(reconciled.missing_reconciled);
    REQUIRE(reconciled.missing == 1);
    const auto present = fileweaver::search_files(search);
    REQUIRE(present.size() == 1);
    REQUIRE(present[0].file_id == initial[1].file_id);
    REQUIRE(fileweaver::catalog_status(fixture.path()).missing_files == 1);
    {
        std::ofstream file(removed);
        file << "restored";
    }
    REQUIRE(fileweaver::index_directory(index, [] { return std::int64_t{2}; }).added == 0);
    const auto restored = fileweaver::search_files(search);
    REQUIRE(restored.size() == 2);
    REQUIRE(restored[0].file_id == initial[0].file_id);
    REQUIRE(restored[0].size_bytes == 8);
    REQUIRE(fileweaver::catalog_status(fixture.path()).missing_files == 0);
}

TEST_CASE("Prefix reads v2 with filters without migration or scan writes", "[prefix-search]") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/legacy','/legacy',0);"
        "INSERT INTO files(root_id,normalized_path,display_path,name,extension,size_bytes,"
        "modified_at,indexed_at,is_present) VALUES"
        "(1,'/legacy/a','/legacy/a','Report-a.txt','.txt',5,20,0,1),"
        "(1,'/legacy/b','/legacy/b','Report-b.txt','.txt',5,30,0,1),"
        "(1,'/legacy/c','/legacy/c','Report-c.txt','.txt',5,20,0,0),"
        "(1,'/legacy/d','/legacy/d','xReport.txt','.txt',5,20,0,1);"
        "DROP TABLE root_exclusions; DROP TABLE root_scan_options; PRAGMA user_version=2");
    fileweaver::SearchRequest search{fixture.path(), "report", fileweaver::SearchMode::prefix};
    search.filters.extension = ".TXT";
    search.filters.minimum_size = 5;
    search.filters.maximum_size = 5;
    search.filters.modified_after = 10;
    search.filters.modified_before = 30;
    search.sort = fileweaver::SearchSort::size;
    search.descending = true;
    search.limit = 1;
    const auto found = fileweaver::search_files(search);
    REQUIRE(found.size() == 1);
    REQUIRE(found[0].name == "Report-a.txt");
    search.filters.modified_before = 31;
    search.limit = 100;
    const auto ties = fileweaver::search_files(search);
    REQUIRE(ties.size() == 2);
    REQUIRE(ties[0].name == "Report-a.txt");
    REQUIRE(ties[1].name == "Report-b.txt");
    const auto status = fileweaver::catalog_status(fixture.path());
    REQUIRE(status.schema_version == 2);
    REQUIRE(status.present_files == 3);
    REQUIRE(status.missing_files == 1);
    REQUIRE(status.scan_records == 0);
    sql(fixture.path(), "CREATE TEMP TABLE unchanged(v INTEGER CHECK(v=2));"
                        "INSERT INTO unchanged SELECT user_version FROM pragma_user_version;"
                        "CREATE TEMP TABLE no_policy(v INTEGER CHECK(v=0));"
                        "INSERT INTO no_policy SELECT count(*) FROM sqlite_master "
                        "WHERE name IN ('root_scan_options','root_exclusions')");
}

TEST_CASE("Prefix validates boundaries and cancellation without poisoning later searches",
          "[prefix-search]") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/test','/test',0);"
        "INSERT INTO files(root_id,normalized_path,display_path,name,extension,size_bytes,"
        "modified_at,indexed_at) VALUES(1,'/test/a','/test/a','Report.txt','.txt',3,0,0)");
    fileweaver::SearchRequest search{fixture.path(), "REP", fileweaver::SearchMode::prefix};
    const auto initial = fileweaver::search_files(search);
    REQUIRE(initial.size() == 1);
    std::stop_source stop;
    REQUIRE(stop.request_stop());
    try {
        static_cast<void>(fileweaver::search_files(search, stop.get_token()));
        FAIL("Pre-cancelled prefix search returned successfully");
    } catch (const fileweaver::StorageError &error) {
        REQUIRE(error.exit_code() == 1);
        REQUIRE(std::string(error.what()) == "Search cancelled");
    }
    const auto recovered = fileweaver::search_files(search);
    REQUIRE(recovered.size() == 1);
    REQUIRE(recovered[0].file_id == initial[0].file_id);
    for (const auto &invalid : {std::string{}, std::string(4097, 'x'), std::string("a\0b", 3)}) {
        search.query = invalid;
        try {
            static_cast<void>(fileweaver::search_files(search));
            FAIL("Invalid prefix query returned successfully");
        } catch (const fileweaver::StorageError &error) {
            REQUIRE(error.exit_code() == 2);
        }
    }
    search.query.assign(4096, 'x');
    REQUIRE(fileweaver::search_files(search).empty());
    search.query = "REP";
    for (const std::size_t limit : {std::size_t{0}, std::size_t{10001}}) {
        search.limit = limit;
        REQUIRE_THROWS_AS(fileweaver::search_files(search), fileweaver::StorageError);
    }
    for (const std::size_t limit : {std::size_t{1}, std::size_t{10000}}) {
        search.limit = limit;
        REQUIRE(fileweaver::search_files(search).size() == 1);
    }
    REQUIRE(fileweaver::catalog_status(fixture.path()).scan_records == 0);
}

TEST_CASE("Search ties are deterministic and limits are applied after sorting") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directories(root / "z");
    std::filesystem::create_directories(root / "a");
    {
        std::ofstream file(root / "z" / "same.txt");
    }
    {
        std::ofstream file(root / "a" / "same.txt");
    }
    fileweaver::initialize_database(fixture.path());
    static_cast<void>(
        fileweaver::index_directory({fixture.path(), root, {}}, [] { return std::int64_t{0}; }));
    fileweaver::SearchRequest request{fixture.path(), "same", fileweaver::SearchMode::substring,
                                      100};
    const auto results = fileweaver::search_files(request);
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].display_path < results[1].display_path);
    request.limit = 1;
    REQUIRE(fileweaver::search_files(request)[0].file_id == results[0].file_id);
    REQUIRE(fileweaver::search_files(request).size() == 1);
}

TEST_CASE("Search uses indexed presence without reading source files") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    {
        std::ofstream file(root / "gone.txt");
    }
    fileweaver::initialize_database(fixture.path());
    const fileweaver::IndexRequest index{fixture.path(), root, {}};
    static_cast<void>(fileweaver::index_directory(index, [] { return std::int64_t{0}; }));
    const fileweaver::SearchRequest search{fixture.path(), "gone"};
    std::filesystem::remove(root / "gone.txt");
    REQUIRE(fileweaver::search_files(search).size() == 1);
    REQUIRE(fileweaver::index_directory(index, [] { return std::int64_t{1}; }).missing == 1);
    REQUIRE(fileweaver::search_files(search).empty());
}

TEST_CASE("Search validates inputs, initialized schemas and cancellation") {
    Fixture fixture;
    fileweaver::SearchRequest request{fixture.path(), "data"};
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    REQUIRE_FALSE(std::filesystem::exists(fixture.path()));
    fileweaver::initialize_database(fixture.path());
    REQUIRE(fileweaver::search_files(request).empty());
    for (const std::size_t limit : {std::size_t{0}, std::size_t{10001}}) {
        request.limit = limit;
        REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    }
    request.limit = 100;
    for (const auto &invalid : {std::string{}, std::string(4097, 'x'), std::string("a\0b", 3)}) {
        request.query = invalid;
        REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    }
    request.query = "valid";
    std::stop_source stop;
    stop.request_stop();
    REQUIRE_THROWS_AS(fileweaver::search_files(request, stop.get_token()),
                      fileweaver::StorageError);
    sql(fixture.path(), "PRAGMA user_version=99");
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
}

TEST_CASE("Search rejects oversized stored paths instead of allocating unbounded text") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/fixture','/fixture',0);"
        "INSERT INTO "
        "files(root_id,normalized_path,display_path,name,extension,size_bytes,modified_at,indexed_"
        "at) "
        "VALUES(1,'/fixture/data',printf('%.*c',131073,'x'),'data','',0,0,0)");
    REQUIRE_THROWS_AS(fileweaver::search_files({fixture.path(), "data"}), fileweaver::StorageError);
}

TEST_CASE("Search human output quotes paths without terminal control bytes") {
    REQUIRE(fileweaver::cli::quote_path("a\"b\\c") == "\"a\\\"b\\\\c\"");
    REQUIRE(fileweaver::cli::quote_path("line\n\t\x1b\x7f") ==
            "\"line\\u000a\\u0009\\u001b\\u007f\"");
    const std::u8string unicode = u8"資料.txt";
    const std::string name(unicode.begin(), unicode.end());
    REQUIRE(fileweaver::cli::quote_path(name) == '"' + name + '"');
}

TEST_CASE("Search total text budget fails instead of returning partial results") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/fixture','/fixture',0);"
        "WITH RECURSIVE n(v) AS (VALUES(1) UNION ALL SELECT v+1 FROM n WHERE v<130) "
        "INSERT INTO "
        "files(root_id,normalized_path,display_path,name,extension,size_bytes,modified_at,indexed_"
        "at) "
        "SELECT 1,CAST(v AS TEXT),printf('%.*c',131072,'x'),'needle','',0,0,0 FROM n");
    const fileweaver::SearchRequest request{fixture.path(), "needle", fileweaver::SearchMode::exact,
                                            130};
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
}

TEST_CASE("Search extension and inclusive size filters combine before limiting") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directories(root / "a");
    std::filesystem::create_directories(root / "b");
    for (const auto &path : {root / "a" / "data.txt", root / "b" / "data.TXT", root / "data.pdf",
                             root / "data", root / "data.tar.gz"}) {
        std::ofstream file(path);
        file << (path.filename() == "data" ? "" : "abc");
    }
    fileweaver::initialize_database(fixture.path());
    static_cast<void>(
        fileweaver::index_directory({fixture.path(), root, {}}, [] { return std::int64_t{0}; }));
    fileweaver::SearchRequest request{fixture.path(), "data"};
    request.filters.extension = "TXT";
    REQUIRE(fileweaver::search_files(request).size() == 2);
    request.filters.extension = ".txt";
    request.filters.minimum_size = 3;
    request.filters.maximum_size = 3;
    REQUIRE(fileweaver::search_files(request).size() == 2);
    request.limit = 1;
    REQUIRE(fileweaver::search_files(request)[0].name == "data.TXT");
    request.filters.minimum_size = 4;
    request.filters.maximum_size = 5;
    REQUIRE(fileweaver::search_files(request).empty());
    request.filters.extension = "";
    request.filters.minimum_size = 0;
    request.filters.maximum_size = 0;
    const auto zero = fileweaver::search_files(request);
    REQUIRE(zero.size() == 1);
    REQUIRE(zero[0].name == "data");
    request.filters.minimum_size.reset();
    request.filters.maximum_size.reset();
    request.filters.extension = "gz";
    REQUIRE(fileweaver::search_files(request)[0].name == "data.tar.gz");
    request.filters.extension = "tar.gz";
    REQUIRE(fileweaver::search_files(request).empty());
    request.filters.extension = "' OR 1=1 --";
    REQUIRE(fileweaver::search_files(request).empty());
    request.mode = fileweaver::SearchMode::exact;
    request.query = "data.TXT";
    request.filters.extension = "txt";
    request.filters.maximum_size = (std::numeric_limits<std::int64_t>::max)();
    REQUIRE(fileweaver::search_files(request).size() == 1);
    request.filters.minimum_size = (std::numeric_limits<std::int64_t>::max)();
    REQUIRE(fileweaver::search_files(request).empty());
}

TEST_CASE("Search root filter selects exact registered membership without requiring live roots") {
    Fixture fixture;
    const auto first = fixture.directory / "first";
    const auto second = fixture.directory / "second";
    std::filesystem::create_directory(first);
    std::filesystem::create_directory(second);
    {
        std::ofstream file(first / "data.txt");
    }
    {
        std::ofstream file(second / "data.txt");
    }
    fileweaver::initialize_database(fixture.path());
    const auto clock = [] { return std::int64_t{0}; };
    static_cast<void>(fileweaver::index_directory({fixture.path(), first, {}}, clock));
    static_cast<void>(fileweaver::index_directory({fixture.path(), second, {}}, clock));
    fileweaver::SearchRequest request{fixture.path(), "data"};
    REQUIRE(fileweaver::search_files(request).size() == 2);
    request.filters.root = first / ".";
    auto results = fileweaver::search_files(request);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].display_path.find("/first/") != std::string::npos);
    request.filters.root = fixture.directory;
    REQUIRE(fileweaver::search_files(request).empty());
    const auto original_directory = std::filesystem::current_path();
    {
        const ScopedCurrentDirectory selected(fixture.directory);
        request.filters.root = std::filesystem::path("second");
        REQUIRE(fileweaver::search_files(request).size() == 1);
        std::filesystem::remove(second / "data.txt");
        std::filesystem::remove(second);
        REQUIRE(fileweaver::search_files(request).size() == 1);
    }
    REQUIRE(std::filesystem::current_path() == original_directory);
    request.filters.root = fixture.directory / "unregistered";
    REQUIRE(fileweaver::search_files(request).empty());
}

TEST_CASE("Search rejects invalid filter ranges and malformed extension/root inputs") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    fileweaver::SearchRequest request{fixture.path(), "data"};
    request.filters.minimum_size = -1;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.filters.minimum_size = 0;
    request.filters.maximum_size = -1;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.filters.minimum_size = 2;
    request.filters.maximum_size = 1;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.filters = {};
    for (const auto &extension : {std::string("a/b"), std::string("a\\b"), std::string("a\0b", 3),
                                  std::string(4097, 'x')}) {
        request.filters.extension = extension;
        REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    }
    request.filters = {};
    request.filters.root = std::filesystem::path{};
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    auto native = fixture.directory.native();
    native.push_back(std::filesystem::path::value_type{});
    request.filters.root = std::filesystem::path(native);
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
}

TEST_CASE("Refresh reuses saved exclusions and never scans unregistered roots") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directories(root / "private");
    {
        std::ofstream file(root / "public.txt");
    }
    {
        std::ofstream file(root / "private" / "secret.txt");
    }
    std::filesystem::create_directory(fixture.directory / "unregistered");
    {
        std::ofstream file(fixture.directory / "unregistered" / "outside.txt");
    }
    fileweaver::initialize_database(fixture.path());
    const auto clock = [] { return std::int64_t{0}; };
    fileweaver::IndexRequest request{fixture.path(), root, {{"private"}}};
    REQUIRE(fileweaver::index_directory(request, clock).added == 1);
    {
        std::ofstream file(root / "new.txt");
    }
    const auto refreshed = fileweaver::refresh_index(fixture.path(), clock);
    REQUIRE(refreshed.complete);
    REQUIRE(refreshed.roots.size() == 1);
    REQUIRE(refreshed.roots[0].attempted);
    REQUIRE(require_index(refreshed.roots.at(0)).added == 1);
    REQUIRE_FALSE(require_index(refreshed.roots.at(0)).missing_reconciled);
    REQUIRE(fileweaver::search_files({fixture.path(), "secret"}).empty());
    REQUIRE(fileweaver::search_files({fixture.path(), "outside"}).empty());
    std::filesystem::create_directory(root / "nested");
    {
        std::ofstream file(root / "nested" / "child.txt");
    }
    request.scan.maximum_depth = 1;
    REQUIRE_FALSE(fileweaver::index_directory(request, clock).scan.complete);
    const auto bounded = fileweaver::refresh_index(fixture.path(), clock);
    REQUIRE_FALSE(bounded.complete);
    REQUIRE(require_index(bounded.roots.at(0)).scan.errors == 1);
}

TEST_CASE("Refresh cancellation between roots preserves completed and unattempted results") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    const auto first = fixture.directory / "a-first";
    const auto second = fixture.directory / "b-second";
    for (const auto &root : {first, second}) {
        std::filesystem::create_directory(root);
        static_cast<void>(fileweaver::index_directory({fixture.path(), root, {}},
                                                      [] { return std::int64_t{0}; }));
        std::ofstream file(root / "new.txt");
    }
    std::stop_source stop;
    int clock_calls = 0;
    const auto result = fileweaver::refresh_index(
        fixture.path(),
        [&] {
            if (++clock_calls == 2) {
                stop.request_stop();
            }
            return std::int64_t{0};
        },
        stop.get_token());
    REQUIRE(result.cancelled);
    REQUIRE_FALSE(result.complete);
    REQUIRE(result.roots.size() == 2);
    REQUIRE(result.roots[0].exit_code == 0);
    REQUIRE(require_index(result.roots.at(0)).added == 1);
    REQUIRE_FALSE(result.roots[1].attempted);
    REQUIRE_FALSE(result.roots[1].index.has_value());
}

TEST_CASE("Refresh isolates missing roots, updates healthy roots and reports cancellation") {
    Fixture fixture;
    const auto first = fixture.directory / "a-missing";
    const auto second = fixture.directory / "b-healthy";
    std::filesystem::create_directory(first);
    std::filesystem::create_directory(second);
    {
        std::ofstream file(first / "retained.txt");
    }
    {
        std::ofstream file(second / "old.txt");
    }
    fileweaver::initialize_database(fixture.path());
    const auto clock = [] { return std::int64_t{0}; };
    static_cast<void>(fileweaver::index_directory({fixture.path(), first, {}}, clock));
    static_cast<void>(fileweaver::index_directory({fixture.path(), second, {}}, clock));
    std::filesystem::remove(first / "retained.txt");
    std::filesystem::remove(first);
    std::filesystem::remove(second / "old.txt");
    {
        std::ofstream file(second / "new.txt");
    }
    const auto refreshed = fileweaver::refresh_index(fixture.path(), clock);
    REQUIRE_FALSE(refreshed.complete);
    REQUIRE(refreshed.roots.size() == 2);
    REQUIRE(refreshed.roots[0].exit_code == 3);
    REQUIRE(refreshed.roots[1].exit_code == 0);
    REQUIRE(require_index(refreshed.roots.at(1)).added == 1);
    REQUIRE(require_index(refreshed.roots.at(1)).missing == 1);
    REQUIRE(fileweaver::search_files({fixture.path(), "retained"}).size() == 1);
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = fileweaver::refresh_index(fixture.path(), clock, stop.get_token());
    REQUIRE(cancelled.cancelled);
    REQUIRE_FALSE(cancelled.complete);
    for (const auto &result : cancelled.roots) {
        REQUIRE_FALSE(result.attempted);
        REQUIRE_FALSE(result.index.has_value());
    }
}

TEST_CASE("V2 upgrade preserves metadata but requires explicit scan policy confirmation") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directories(root / "private");
    {
        std::ofstream file(root / "public.txt");
    }
    {
        std::ofstream file(root / "private" / "secret.txt");
    }
    fileweaver::initialize_database(fixture.path());
    const auto clock = [] { return std::int64_t{0}; };
    fileweaver::IndexRequest request{fixture.path(), root, {{"private"}}};
    static_cast<void>(fileweaver::index_directory(request, clock));
    sql(fixture.path(),
        "DROP TABLE root_exclusions; DROP TABLE root_scan_options; PRAGMA user_version=2");
    REQUIRE(fileweaver::search_files({fixture.path(), "public"}).size() == 1);
    REQUIRE_THROWS_AS(fileweaver::refresh_index(fixture.path(), clock), fileweaver::StorageError);
    fileweaver::initialize_database(fixture.path());
    const auto unknown = fileweaver::refresh_index(fixture.path(), clock);
    REQUIRE_FALSE(unknown.complete);
    REQUIRE(unknown.roots[0].exit_code == 5);
    REQUIRE_FALSE(unknown.roots[0].attempted);
    REQUIRE(fileweaver::search_files({fixture.path(), "secret"}).empty());
    static_cast<void>(fileweaver::index_directory(request, clock));
    REQUIRE(fileweaver::refresh_index(fixture.path(), clock).complete);
}

TEST_CASE("V3 migration failure rolls back policy tables and schema version") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "DROP TABLE root_exclusions; DROP TABLE root_scan_options; PRAGMA user_version=2;"
        "CREATE VIEW root_exclusions AS SELECT 1 AS collision");
    REQUIRE_THROWS_AS(fileweaver::initialize_database(fixture.path()), fileweaver::StorageError);
    sql(fixture.path(),
        "CREATE TEMP TABLE version_assertion(v INTEGER CHECK(v=2));"
        "INSERT INTO version_assertion SELECT user_version FROM pragma_user_version;"
        "CREATE TEMP TABLE absent(v INTEGER CHECK(v=0));"
        "INSERT INTO absent SELECT count(*) FROM sqlite_schema WHERE name='root_scan_options';"
        "DROP VIEW root_exclusions");
    fileweaver::initialize_database(fixture.path());
}

TEST_CASE("Refresh refuses policy changes after snapshot rather than expanding scope") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    {
        std::ofstream file(root / "public.txt");
    }
    fileweaver::initialize_database(fixture.path());
    static_cast<void>(
        fileweaver::index_directory({fixture.path(), root, {}}, [] { return std::int64_t{0}; }));
    std::filesystem::create_directory(root / "newly-private");
    {
        std::ofstream file(root / "newly-private" / "secret.txt");
    }
    bool changed = false;
    const auto refreshed = fileweaver::refresh_index(fixture.path(), [&] {
        if (!changed) {
            changed = true;
            sql(fixture.path(), "INSERT INTO root_exclusions(root_id,ordinal,path) SELECT "
                                "id,0,'newly-private' FROM roots");
        }
        return std::int64_t{0};
    });
    REQUIRE(changed);
    REQUIRE_FALSE(refreshed.complete);
    REQUIRE(refreshed.roots[0].error.find("changed") != std::string::npos);
    REQUIRE(fileweaver::search_files({fixture.path(), "secret"}).empty());
}

TEST_CASE("Refresh handles empty catalogs and bounds root inventory before scanning") {
    Fixture fixture;
    const auto clock = [] { return std::int64_t{0}; };
    REQUIRE_THROWS_AS(fileweaver::refresh_index(fixture.path(), clock), fileweaver::StorageError);
    REQUIRE_FALSE(std::filesystem::exists(fixture.path()));
    fileweaver::initialize_database(fixture.path());
    REQUIRE(fileweaver::refresh_index(fixture.path(), clock).complete);
    sql(fixture.path(),
        "WITH RECURSIVE n(v) AS (VALUES(1) UNION ALL SELECT v+1 FROM n WHERE v<10001) "
        "INSERT INTO roots(normalized_path,display_path,added_at) SELECT "
        "'/fixture/'||v,'/fixture/'||v,0 FROM n");
    REQUIRE_THROWS_AS(fileweaver::refresh_index(fixture.path(), clock), fileweaver::StorageError);
    sql(fixture.path(), "CREATE TEMP TABLE absent(v INTEGER CHECK(v=0));"
                        "INSERT INTO absent SELECT count(*) FROM scans");
}

TEST_CASE("Catalog inspection exposes saved policies and does not revalidate offline sources") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    REQUIRE(fileweaver::list_roots(fixture.path()).empty());
    REQUIRE(fileweaver::catalog_status(fixture.path()).present_files == 0);
    const auto first = fixture.directory / "a-empty";
    const auto second = fixture.directory / "b-source";
    std::filesystem::create_directory(first);
    std::filesystem::create_directories(second / "private");
    {
        std::ofstream file(second / "public.txt");
    }
    {
        std::ofstream file(second / "private" / "secret.txt");
    }
    const auto clock = [] { return std::int64_t{42}; };
    static_cast<void>(fileweaver::index_directory({fixture.path(), first, {}}, clock));
    fileweaver::IndexRequest request{fixture.path(), second, {{"private"}, 3}};
    static_cast<void>(fileweaver::index_directory(request, clock));
    auto roots = fileweaver::list_roots(fixture.path());
    REQUIRE(roots.size() == 2);
    REQUIRE(roots[0].path < roots[1].path);
    REQUIRE(roots[1].policy_known);
    REQUIRE(roots[1].maximum_depth == std::optional<std::int64_t>{3});
    REQUIRE(roots[1].last_scan_at == std::optional<std::int64_t>{42});
    REQUIRE(roots[1].last_scan_status == "completed");
    REQUIRE(roots[1].exclusions == std::vector<std::string>{"private"});
    std::filesystem::remove(second / "public.txt");
    std::filesystem::remove(second / "private" / "secret.txt");
    std::filesystem::remove(second / "private");
    std::filesystem::remove(second);
    REQUIRE(fileweaver::list_roots(fixture.path()).size() == 2);
    const auto status = fileweaver::catalog_status(fixture.path());
    REQUIRE(status.schema_version == 3);
    REQUIRE(status.roots == 2);
    REQUIRE(status.present_files == 1);
    REQUIRE(status.missing_files == 0);
    REQUIRE(status.scan_records == 2);
    REQUIRE(status.running_scan_records == 0);
}

TEST_CASE("Catalog status counts present, missing and recorded running scans") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    {
        std::ofstream file(root / "keep");
    }
    {
        std::ofstream file(root / "remove");
    }
    fileweaver::initialize_database(fixture.path());
    const auto clock = [] { return std::int64_t{0}; };
    static_cast<void>(fileweaver::index_directory({fixture.path(), root, {}}, clock));
    std::filesystem::remove(root / "remove");
    static_cast<void>(fileweaver::index_directory({fixture.path(), root, {}}, clock));
    sql(fixture.path(),
        "INSERT INTO scans(root_id,started_at,status) SELECT id,0,'running' FROM roots");
    const auto status = fileweaver::catalog_status(fixture.path());
    REQUIRE(status.roots == 1);
    REQUIRE(status.present_files == 1);
    REQUIRE(status.missing_files == 1);
    REQUIRE(status.scan_records == 3);
    REQUIRE(status.running_scan_records == 1);
}

TEST_CASE(
    "Catalog inspection supports v2 without migration and rejects cancellation/invalid storage") {
    Fixture fixture;
    REQUIRE_THROWS_AS(fileweaver::list_roots(fixture.path()), fileweaver::StorageError);
    REQUIRE_THROWS_AS(fileweaver::catalog_status(fixture.path()), fileweaver::StorageError);
    REQUIRE_FALSE(std::filesystem::exists(fixture.path()));
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/legacy','/legacy',0);"
        "DROP TABLE root_exclusions; DROP TABLE root_scan_options; PRAGMA user_version=2");
    const auto roots = fileweaver::list_roots(fixture.path());
    REQUIRE(roots.size() == 1);
    REQUIRE_FALSE(roots[0].policy_known);
    REQUIRE_FALSE(roots[0].maximum_depth.has_value());
    REQUIRE_FALSE(roots[0].last_scan_at.has_value());
    REQUIRE(roots[0].exclusions.empty());
    REQUIRE(fileweaver::catalog_status(fixture.path()).schema_version == 2);
    sql(fixture.path(), "CREATE TEMP TABLE unchanged(v INTEGER CHECK(v=2));"
                        "INSERT INTO unchanged SELECT user_version FROM pragma_user_version");
    std::stop_source stop;
    stop.request_stop();
    REQUIRE_THROWS_AS(fileweaver::list_roots(fixture.path(), stop.get_token()),
                      fileweaver::StorageError);
    REQUIRE_THROWS_AS(fileweaver::catalog_status(fixture.path(), stop.get_token()),
                      fileweaver::StorageError);
    sql(fixture.path(), "PRAGMA user_version=99");
    REQUIRE_THROWS_AS(fileweaver::list_roots(fixture.path()), fileweaver::StorageError);
    REQUIRE_THROWS_AS(fileweaver::catalog_status(fixture.path()), fileweaver::StorageError);
}

TEST_CASE("Root inventory refuses excessive catalogs and malformed paths") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(), "INSERT INTO roots(normalized_path,display_path,added_at) "
                        "VALUES('bad'||char(0)||'path','bad',0)");
    REQUIRE_THROWS_AS(fileweaver::list_roots(fixture.path()), fileweaver::StorageError);
    sql(fixture.path(), "DELETE FROM roots; WITH RECURSIVE n(v) AS (VALUES(1) UNION ALL SELECT v+1 "
                        "FROM n WHERE v<10001) "
                        "INSERT INTO roots(normalized_path,display_path,added_at) SELECT "
                        "'/fixture/'||v,'/fixture/'||v,0 FROM n");
    REQUIRE_THROWS_AS(fileweaver::list_roots(fixture.path()), fileweaver::StorageError);
    REQUIRE(fileweaver::catalog_status(fixture.path()).roots == 10001);
}

TEST_CASE("JSON strings preserve Unicode and reject malformed UTF-8") {
    REQUIRE(fileweaver::cli::json_string("a\n\"\\") == "\"a\\u000a\\\"\\\\\"");
    const std::u8string unicode = u8"資料😀";
    const std::string valid(unicode.begin(), unicode.end());
    REQUIRE(fileweaver::cli::json_string(valid) == '"' + valid + '"');
    for (const auto &invalid : {std::string("\xff", 1), std::string("\xc0\xaf", 2),
                                std::string("\xe2\x82", 2), std::string("\xed\xa0\x80", 3),
                                std::string("\xf4\x90\x80\x80", 4), std::string("\xc2\x41", 2)}) {
        REQUIRE_THROWS_AS(fileweaver::cli::json_string(invalid), std::runtime_error);
    }
    fileweaver::IndexSummary incomplete;
    incomplete.scan.complete = false;
    incomplete.scan.cancelled = true;
    const auto document = fileweaver::cli::json_index(incomplete);
    REQUIRE(document.find("\"ok\":false") != std::string::npos);
    REQUIRE(document.find("\"cancelled\":true") != std::string::npos);
}

TEST_CASE("Search sorting respects direction, ties, filters and limits") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/fixture','/fixture',0);"
        "INSERT INTO "
        "files(root_id,normalized_path,display_path,name,extension,size_bytes,modified_at,indexed_"
        "at) VALUES"
        "(1,'/a','/a','data-a.txt','.txt',3,10,0),"
        "(1,'/b','/b','data-b.txt','.txt',1,30,0),"
        "(1,'/c','/c','data-c.txt','.txt',3,20,0),"
        "(1,'/d','/d','data-d.pdf','.pdf',8,40,0)");
    fileweaver::SearchRequest request{fixture.path(), "data"};
    auto results = fileweaver::search_files(request);
    REQUIRE(results.size() == 4);
    REQUIRE(results[0].name == "data-a.txt");
    request.descending = true;
    REQUIRE(fileweaver::search_files(request).at(0).name == "data-d.pdf");
    request.sort = fileweaver::SearchSort::size;
    request.descending = false;
    results = fileweaver::search_files(request);
    REQUIRE(results[0].name == "data-b.txt");
    REQUIRE(results[1].name == "data-a.txt");
    REQUIRE(results[2].name == "data-c.txt");
    request.descending = true;
    results = fileweaver::search_files(request);
    REQUIRE(results[0].name == "data-d.pdf");
    REQUIRE(results[1].name == "data-a.txt");
    REQUIRE(results[2].name == "data-c.txt");
    request.filters.extension = "txt";
    request.limit = 1;
    REQUIRE(fileweaver::search_files(request).at(0).name == "data-a.txt");
    request.sort = fileweaver::SearchSort::modified;
    REQUIRE(fileweaver::search_files(request).at(0).name == "data-b.txt");
    request.descending = false;
    REQUIRE(fileweaver::search_files(request).at(0).name == "data-a.txt");
    request.filters.modified_after = 10;
    REQUIRE(fileweaver::search_files(request).at(0).name == "data-c.txt");
    // Deliberately exercise defensive validation of an unknown fixed-underlying enum value.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    request.sort = static_cast<fileweaver::SearchSort>(255);
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
}

TEST_CASE("UTC parser handles epoch, negative time, leap years and nanosecond range endpoints") {
    REQUIRE(fileweaver::parse_utc_time("1970-01-01T00:00:00Z") == 0);
    REQUIRE(fileweaver::parse_utc_time("1969-12-31T23:59:59Z") == -1000000000LL);
    REQUIRE(fileweaver::parse_utc_time("2000-02-29T00:00:00Z") == 951782400000000000LL);
    REQUIRE(fileweaver::parse_utc_time("2024-03-01T00:00:00Z") -
                fileweaver::parse_utc_time("2024-02-29T00:00:00Z") ==
            86400000000000LL);
    REQUIRE(fileweaver::parse_utc_time("1677-09-21T00:12:44Z") == -9223372036000000000LL);
    REQUIRE(fileweaver::parse_utc_time("2262-04-11T23:47:16Z") == 9223372036000000000LL);
}

TEST_CASE("UTC parser rejects invalid calendar, ambiguous zone and overflowing times") {
    for (const auto *invalid : {"",
                                "2024-01-01",
                                "2024-01-01T00:00:00",
                                "2024-01-01T00:00:00+00:00",
                                "2024-01-01T00:00:00.1Z",
                                "2024-01-01t00:00:00Z",
                                "2024-01-01T00:00:00z",
                                "2024-02-30T00:00:00Z",
                                "1900-02-29T00:00:00Z",
                                "2024-13-01T00:00:00Z",
                                "2024-00-01T00:00:00Z",
                                "2024-01-00T00:00:00Z",
                                "2024-01-01T24:00:00Z",
                                "2024-01-01T00:60:00Z",
                                "2024-01-01T00:00:60Z",
                                "0000-01-01T00:00:00Z",
                                "1677-09-21T00:12:43Z",
                                "2262-04-11T23:47:17Z",
                                "9999-12-31T00:00:00Z",
                                "202x-01-01T00:00:00Z",
                                "2024-01-01 00:00:00Z"}) {
        REQUIRE_THROWS_AS(fileweaver::parse_utc_time(invalid), fileweaver::StorageError);
    }
    std::string nul = "2024-01-01T00:00:00Z";
    nul[5] = '\0';
    REQUIRE_THROWS_AS(fileweaver::parse_utc_time(nul), fileweaver::StorageError);
}

TEST_CASE(
    "Search modification filters use strict nanosecond boundaries and combine with other filters") {
    Fixture fixture;
    const auto root = fixture.directory / "source";
    std::filesystem::create_directory(root);
    for (const auto *name : {"data-before.txt", "data-at.txt", "data-after.txt"}) {
        std::ofstream file(root / name);
        file << "abc";
    }
    fileweaver::initialize_database(fixture.path());
    static_cast<void>(
        fileweaver::index_directory({fixture.path(), root, {}}, [] { return std::int64_t{0}; }));
    sql(fixture.path(), "UPDATE files SET modified_at=CASE name WHEN 'data-before.txt' THEN -1 "
                        "WHEN 'data-at.txt' THEN 0 ELSE 1 END");
    fileweaver::SearchRequest request{fixture.path(), "data"};
    request.filters.modified_after = 0;
    auto results = fileweaver::search_files(request);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].name == "data-after.txt");
    request.filters.modified_after.reset();
    request.filters.modified_before = 0;
    results = fileweaver::search_files(request);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].name == "data-before.txt");
    request.filters.modified_after = -1;
    request.filters.modified_before = 1;
    request.filters.extension = "txt";
    request.filters.minimum_size = 3;
    request.filters.maximum_size = 3;
    request.filters.root = root;
    request.limit = 1;
    results = fileweaver::search_files(request);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].name == "data-at.txt");
    request.filters.modified_after = 1;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.filters.modified_before = 0;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.limit = 100;
    request.filters.modified_after = (std::numeric_limits<std::int64_t>::min)();
    request.filters.modified_before = (std::numeric_limits<std::int64_t>::max)();
    REQUIRE(fileweaver::search_files(request).size() == 3);
}

TEST_CASE("Cooperative scanner cancellation stops delivery at exact metadata checkpoints",
          "[cooperative-cancellation]") {
    Fixture fixture;
    const auto root = fixture.directory / "scan-source";
    for (int index = 0; index < 6; ++index) {
        write_cancellation_file(root / ("branch-" + std::to_string(index % 2)) /
                                    ("cancel-" + std::to_string(index) + ".txt"),
                                "payload");
    }
    for (const std::uint64_t checkpoint : {std::uint64_t{2}, std::uint64_t{4}}) {
        std::stop_source stop;
        std::uint64_t delivered = 0;
        const auto cancelled = fileweaver::scan_directory(
            root, {},
            [&](const auto &) {
                if (++delivered == checkpoint) {
                    stop.request_stop();
                }
            },
            stop.get_token());
        REQUIRE(delivered == checkpoint);
        REQUIRE(cancelled.files == checkpoint);
        REQUIRE(cancelled.cancelled);
        REQUIRE_FALSE(cancelled.complete);
        REQUIRE(cancelled.errors == 0);
        REQUIRE(cancelled.skipped == 0);
        std::uint64_t after_stop = 0;
        const auto before_delivery = fileweaver::scan_directory(
            root, {}, [&](const auto &) { ++after_stop; }, stop.get_token());
        REQUIRE(before_delivery.cancelled);
        REQUIRE_FALSE(before_delivery.complete);
        REQUIRE(before_delivery.files == 0);
        REQUIRE(after_stop == 0);
        const auto recovered =
            fileweaver::scan_directory(root, {}, [&](const auto &) { ++after_stop; });
        REQUIRE(recovered.complete);
        REQUIRE_FALSE(recovered.cancelled);
        REQUIRE(recovered.files == 6);
        REQUIRE(after_stop == 6);
        REQUIRE(recovered.errors == 0);
    }
}

TEST_CASE("Cooperative index cancellation retains old tuples IDs and recovers",
          "[cooperative-cancellation]") {
    Fixture fixture;
    const auto root = fixture.directory / "index-source";
    write_cancellation_file(root / "cancel-a.txt", "old");
    write_cancellation_file(root / "cancel-b.txt", "old");
    fileweaver::initialize_database(fixture.path());
    REQUIRE(fileweaver::index_directory({fixture.path(), root, {}}, [] {
                return std::int64_t{1};
            }).added == 2);
    const auto before = cancellation_snapshot(fixture.path());
    REQUIRE(before.size() == 2);
    write_cancellation_file(root / "cancel-a.txt", "changed");
    REQUIRE(std::filesystem::remove(root / "cancel-b.txt"));
    write_cancellation_file(root / "cancel-c.txt", "new");
    std::stop_source stop;
    bool at_start_clock = false;
    SECTION("stop requested before index invocation") { stop.request_stop(); }
    SECTION("stop requested from injected start clock") { at_start_clock = true; }
    std::int64_t clock_calls = 0;
    const auto clock = [&] {
        if (++clock_calls == 1 && at_start_clock) {
            stop.request_stop();
        }
        return std::int64_t{100} + clock_calls;
    };
    const auto cancelled =
        fileweaver::index_directory({fixture.path(), root, {}}, clock, stop.get_token());
    require_cancelled_before_delivery(cancelled);
    REQUIRE(clock_calls == 2);
    REQUIRE(cancellation_snapshot(fixture.path()) == before);
    const auto status = fileweaver::catalog_status(fixture.path());
    REQUIRE(status.present_files == 2);
    REQUIRE(status.missing_files == 0);
    REQUIRE(status.scan_records == 2);
    REQUIRE(status.running_scan_records == 0);
    const auto roots = fileweaver::list_roots(fixture.path());
    REQUIRE(roots.size() == 1);
    REQUIRE(roots[0].last_scan_status == "cancelled");
    REQUIRE(roots[0].policy_known);
    REQUIRE(roots[0].exclusions.empty());
    REQUIRE(roots[0].maximum_depth == std::size_t{128});
    const auto recovered =
        fileweaver::index_directory({fixture.path(), root, {}}, [] { return std::int64_t{1000}; });
    REQUIRE(recovered.scan.complete);
    REQUIRE_FALSE(recovered.scan.cancelled);
    REQUIRE(recovered.added == 1);
    REQUIRE(recovered.updated == 1);
    REQUIRE(recovered.missing == 1);
    REQUIRE(recovered.missing_reconciled);
    const auto after = cancellation_snapshot(fixture.path());
    REQUIRE(after.size() == 2);
    REQUIRE(std::get<0>(after[0]) == std::get<0>(before[0]));
    REQUIRE(std::get<4>(after[0]) == 7);
    REQUIRE(std::get<2>(after[1]) == "cancel-c.txt");
    REQUIRE(std::get<0>(after[1]) != std::get<0>(before[1]));
    REQUIRE(fileweaver::catalog_status(fixture.path()).missing_files == 1);
    REQUIRE(fileweaver::catalog_status(fixture.path()).running_scan_records == 0);
    REQUIRE(fileweaver::list_roots(fixture.path())[0].last_scan_status == "completed");
}

TEST_CASE("Cooperative refresh cancellation preserves per-root checkpoint semantics",
          "[cooperative-cancellation]") {
    Fixture fixture;
    const auto paths = seed_cancellation_roots(fixture);
    const auto before = cancellation_snapshot(fixture.path());
    REQUIRE(before.size() == 2);
    write_cancellation_file(paths[0] / "cancel-a.txt", "changed");
    write_cancellation_file(paths[1] / "cancel-b.txt", "changed");
    enum class Checkpoint : std::uint8_t { before_refresh, first_start, between_roots };
    auto checkpoint = Checkpoint::before_refresh;
    SECTION("pre-cancelled refresh attempts no root") { checkpoint = Checkpoint::before_refresh; }
    SECTION("first start clock cancels that root and skips the next") {
        checkpoint = Checkpoint::first_start;
    }
    SECTION("first finish clock preserves the completed root commit") {
        checkpoint = Checkpoint::between_roots;
    }
    std::stop_source stop;
    if (checkpoint == Checkpoint::before_refresh) {
        stop.request_stop();
    }
    std::int64_t clock_calls = 0;
    const auto clock = [&] {
        ++clock_calls;
        if ((checkpoint == Checkpoint::first_start && clock_calls == 1) ||
            (checkpoint == Checkpoint::between_roots && clock_calls == 2)) {
            stop.request_stop();
        }
        return std::int64_t{100} + clock_calls;
    };
    const auto cancelled = fileweaver::refresh_index(fixture.path(), clock, stop.get_token());
    REQUIRE(cancelled.cancelled);
    REQUIRE_FALSE(cancelled.complete);
    REQUIRE(cancelled.roots.size() == 2);
    REQUIRE(cancelled.roots[0].root.generic_u8string() == paths[0].generic_u8string());
    REQUIRE_FALSE(cancelled.roots[1].attempted);
    REQUIRE_FALSE(cancelled.roots[1].index.has_value());
    REQUIRE(cancelled.roots[1].exit_code == 1);
    if (checkpoint == Checkpoint::before_refresh) {
        REQUIRE(clock_calls == 0);
        REQUIRE_FALSE(cancelled.roots[0].attempted);
        REQUIRE_FALSE(cancelled.roots[0].index.has_value());
    } else {
        REQUIRE(clock_calls == 2);
        REQUIRE(cancelled.roots[0].attempted);
        const auto &first = require_index(cancelled.roots[0]);
        if (checkpoint == Checkpoint::first_start) {
            require_cancelled_before_delivery(first);
            REQUIRE(cancelled.roots[0].exit_code == 1);
        } else {
            REQUIRE(first.scan.complete);
            REQUIRE_FALSE(first.scan.cancelled);
            REQUIRE(first.updated == 1);
            REQUIRE(first.added == 0);
            REQUIRE(first.missing == 0);
            REQUIRE(cancelled.roots[0].exit_code == 0);
        }
    }
    auto expected = before;
    if (checkpoint == Checkpoint::between_roots) {
        std::get<4>(expected[0]) = 7;
    }
    REQUIRE(cancellation_snapshot(fixture.path()) == expected);
    const auto state = fileweaver::catalog_status(fixture.path());
    REQUIRE(state.present_files == 2);
    REQUIRE(state.missing_files == 0);
    REQUIRE(state.scan_records == (checkpoint == Checkpoint::before_refresh ? 2 : 3));
    REQUIRE(state.running_scan_records == 0);
    const auto inspected = fileweaver::list_roots(fixture.path());
    REQUIRE(inspected.size() == 2);
    REQUIRE(inspected[0].last_scan_status ==
            (checkpoint == Checkpoint::first_start ? "cancelled" : "completed"));
    REQUIRE(inspected[1].last_scan_status == "completed");
    for (const auto &root : inspected) {
        REQUIRE(root.policy_known);
        REQUIRE(root.exclusions.empty());
        REQUIRE(root.maximum_depth == std::size_t{128});
    }
    const auto recovered =
        fileweaver::refresh_index(fixture.path(), [] { return std::int64_t{1000}; });
    REQUIRE(recovered.complete);
    REQUIRE_FALSE(recovered.cancelled);
    REQUIRE(recovered.roots.size() == 2);
    for (std::size_t index = 0; index < recovered.roots.size(); ++index) {
        REQUIRE(recovered.roots[index].attempted);
        REQUIRE(recovered.roots[index].exit_code == 0);
        const auto &result = require_index(recovered.roots[index]);
        REQUIRE(result.scan.complete);
        REQUIRE_FALSE(result.scan.cancelled);
        REQUIRE(result.updated == (index == 0 && checkpoint == Checkpoint::between_roots ? 0 : 1));
        REQUIRE(result.added == 0);
        REQUIRE(result.missing == 0);
    }
    for (auto &row : expected) {
        std::get<4>(row) = 7;
    }
    REQUIRE(cancellation_snapshot(fixture.path()) == expected);
    const auto final_status = fileweaver::catalog_status(fixture.path());
    REQUIRE(final_status.scan_records == (checkpoint == Checkpoint::before_refresh ? 4 : 5));
    REQUIRE(final_status.running_scan_records == 0);
    for (const auto &root : fileweaver::list_roots(fixture.path())) {
        REQUIRE(root.last_scan_status == "completed");
    }
}

TEST_CASE("Fuzzy matching uses Unicode scalars, tokens, typos and bounded inputs") {
    using fileweaver::FuzzyMatcher;
    REQUIRE(FuzzyMatcher("img proc fin").score("image_processing_final_report.pdf").has_value());
    REQUIRE(FuzzyMatcher("reprot").score("report.txt").has_value());
    REQUIRE(FuzzyMatcher("reprt").score("report.txt").has_value());
    REQUIRE_FALSE(FuzzyMatcher("zzzzzz").score("report.txt").has_value());
    REQUIRE_FALSE(FuzzyMatcher("日本😀").candidate("noise123.txt"));
    REQUIRE(FuzzyMatcher("abc").candidate("bca"));
    REQUIRE_FALSE(FuzzyMatcher("abc").score("bca").has_value());
    REQUIRE(FuzzyMatcher("日本😀").score("日本語😀.txt").has_value());
    REQUIRE(FuzzyMatcher("REPORT").score("report.txt") ==
            FuzzyMatcher("report").score("REPORT.txt"));
    REQUIRE_FALSE(FuzzyMatcher("É").score("é.txt").has_value());
    REQUIRE_FALSE(FuzzyMatcher("ab").score("ac.txt").has_value());
    REQUIRE_THROWS_AS(FuzzyMatcher("   "), fileweaver::StorageError);
    REQUIRE_THROWS_AS(FuzzyMatcher(std::string(129, 'a')), fileweaver::StorageError);
    REQUIRE_THROWS_AS(FuzzyMatcher(std::string("\xff", 1)), fileweaver::StorageError);
    REQUIRE_THROWS_AS(FuzzyMatcher("a a a a a a a a a a a a a a a a a"), fileweaver::StorageError);
    REQUIRE_THROWS_AS((FuzzyMatcher("report", {-1, 0, 0, 0, 0})), fileweaver::StorageError);
    REQUIRE_THROWS_AS(FuzzyMatcher("report").candidate(std::string(1025, 'a')),
                      fileweaver::StorageError);
    std::stop_source stopped;
    stopped.request_stop();
    REQUIRE_THROWS_AS(FuzzyMatcher("report").candidate("report.txt", stopped.get_token()),
                      fileweaver::StorageError);
    REQUIRE_THROWS_AS(FuzzyMatcher("report").score("report.txt", stopped.get_token()),
                      fileweaver::StorageError);
    // The q-gram candidate gate must retain every generated one-edit neighbor.
    for (const auto &query : {"abc", "report", "aaaaaa", "日本語abc"}) {
        const FuzzyMatcher matcher(query);
        const std::string original(query);
        // ASCII edits include repeated q-grams and both sides of the tolerance boundary.
        for (std::size_t i = 0; i < original.size(); ++i) {
            if (static_cast<unsigned char>(original[i]) >= 128) {
                continue;
            }
            auto changed = original;
            changed.erase(i, 1);
            REQUIRE(matcher.candidate(changed));
            REQUIRE(matcher.score(changed).has_value());
            changed = original;
            changed[i] = 'z';
            REQUIRE(matcher.candidate(changed));
            REQUIRE(matcher.score(changed).has_value());
            changed = original;
            changed.insert(i, "z");
            REQUIRE(matcher.candidate(changed));
            REQUIRE(matcher.score(changed).has_value());
        }
    }
}

TEST_CASE("Fuzzy search ranks a filtered snapshot with deterministic ties and candidate budget") {
    Fixture fixture;
    fileweaver::initialize_database(fixture.path());
    sql(fixture.path(),
        "INSERT INTO roots(normalized_path,display_path,added_at) VALUES('/fixture','/fixture',0);"
        "INSERT INTO "
        "files(root_id,normalized_path,display_path,name,extension,size_bytes,modified_at,indexed_"
        "at,is_present) VALUES"
        "(1,'/z','/z','report.txt','.txt',3,10,0,1),"
        "(1,'/a','/a','report.txt','.txt',3,10,0,1),"
        "(1,'/b','/b','reprot.txt','.txt',4,20,0,1),"
        "(1,'/c','/c','report.pdf','.pdf',5,30,0,1),"
        "(1,'/missing','/missing','report','.txt',3,10,0,0),"
        "(1,'/unicode','/unicode','日本語😀.txt','.txt',3,10,0,1)");
    fileweaver::SearchRequest request{fixture.path(), "report", fileweaver::SearchMode::fuzzy};
    request.sort = fileweaver::SearchSort::relevance;
    auto results = fileweaver::search_files(request);
    REQUIRE(results.size() == 4);
    REQUIRE(results[0].name == "report.pdf");
    REQUIRE(results[1].display_path == "/a");
    REQUIRE(results[2].display_path == "/z");
    request.filters.extension = "txt";
    request.limit = 1;
    REQUIRE(fileweaver::search_files(request)[0].display_path == "/a");
    request.fuzzy.weights = {0, 0, 0, 0, 10000};
    REQUIRE(fileweaver::search_files(request)[0].name == "reprot.txt");
    request.filters.minimum_size = 5;
    REQUIRE(fileweaver::search_files(request).empty());
    request.filters = {};
    request.fuzzy.candidate_limit = 1;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.fuzzy.candidate_limit = 0;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.fuzzy.candidate_limit = 4096;
    request.query = "日本😀";
    REQUIRE(fileweaver::search_files(request)[0].name == "日本語😀.txt");
    sql(fixture.path(), "PRAGMA user_version=2");
    REQUIRE(fileweaver::search_files(request).size() == 1);
    std::stop_source stopped;
    stopped.request_stop();
    REQUIRE_THROWS_AS(fileweaver::search_files(request, stopped.get_token()),
                      fileweaver::StorageError);
    request.descending = true;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.descending = false;
    request.sort = fileweaver::SearchSort::name;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
    request.sort = fileweaver::SearchSort::relevance;
    request.mode = fileweaver::SearchMode::substring;
    REQUIRE_THROWS_AS(fileweaver::search_files(request), fileweaver::StorageError);
}

TEST_CASE("Watch schedule bounds bursts and reconciles silent gaps with monotonic time") {
    using namespace std::chrono_literals;
    const auto begin = fileweaver::WatchSchedule::Time{};
    fileweaver::WatchSchedule schedule(begin);
    REQUIRE(schedule.due(begin));
    schedule.completed(begin, true);
    REQUIRE_FALSE(schedule.due(begin + 59s));
    REQUIRE(schedule.due(begin + 60s));
    schedule.changed(begin + 1s);
    REQUIRE_FALSE(schedule.due(begin + 1499ms));
    REQUIRE(schedule.due(begin + 1500ms));
    schedule.changed(begin + 1400ms);
    schedule.changed(begin + 1800ms);
    schedule.changed(begin + 2900ms);
    REQUIRE_FALSE(schedule.due(begin + 2999ms));
    REQUIRE(schedule.due(begin + 3s)); // Max 2 s from first event.
    schedule.completed(begin + 3s, false);
    schedule.urgent(begin + 4s);
    REQUIRE_FALSE(schedule.due(begin + 7999ms));
    REQUIRE(schedule.due(begin + 8s));
    schedule.completed(begin + 8s, true);
    schedule.urgent(begin + 9s);
    REQUIRE(schedule.due(begin + 9s));
}

TEST_CASE("Single-root refresh preserves policy, scope and registration identity") {
    Fixture fixture;
    const auto a = fixture.directory / "A";
    const auto b = fixture.directory / "B";
    std::filesystem::create_directories(a / "skip");
    std::filesystem::create_directory(b);
    {
        std::ofstream(a / "one.txt") << "old";
        std::ofstream(b / "other.txt") << "old";
    }
    fileweaver::initialize_database(fixture.path());
    const auto clock = [] { return std::int64_t{1700000000000000000}; };
    fileweaver::IndexRequest first{fixture.path(), a, {{a / "skip"}, 3}};
    REQUIRE(fileweaver::index_directory(first, clock).scan.complete);
    REQUIRE(fileweaver::index_directory({fixture.path(), b, {}}, clock).scan.complete);
    const auto roots = fileweaver::list_roots(fixture.path());
    REQUIRE(roots.size() == 2);
    fileweaver::RootRefreshRequest request{fixture.path(), roots[0].root_id,
                                           std::filesystem::canonical(a)};
    {
        std::ofstream(a / "new.txt") << "new";
        std::ofstream(a / "skip" / "secret.txt") << "secret";
        std::ofstream(b / "unscanned.txt") << "new";
    }
    std::filesystem::remove(a / "one.txt");
    auto result = fileweaver::refresh_root(request, clock);
    REQUIRE(result.exit_code == 0);
    const auto &indexed = require_index(result);
    REQUIRE(indexed.added == 1);
    REQUIRE(indexed.missing == 0);
    REQUIRE_FALSE(indexed.missing_reconciled);
    REQUIRE(fileweaver::search_files({fixture.path(), "one.txt"}).size() == 1);
    REQUIRE(fileweaver::search_files({fixture.path(), "new.txt"}).size() == 1);
    REQUIRE(fileweaver::search_files({fixture.path(), "secret"}).empty());
    REQUIRE(fileweaver::search_files({fixture.path(), "unscanned"}).empty());
    const auto count = fileweaver::catalog_status(fixture.path()).scan_records;
    request.expected_root = b;
    REQUIRE_THROWS_AS(fileweaver::refresh_root(request, clock), fileweaver::StorageError);
    REQUIRE(fileweaver::catalog_status(fixture.path()).scan_records == count);
    request.expected_root = a;
    std::stop_source stopped;
    stopped.request_stop();
    REQUIRE_FALSE(fileweaver::refresh_root(request, clock, stopped.get_token()).attempted);
    sql(fixture.path(), "DELETE FROM root_scan_options WHERE root_id=1");
    result = fileweaver::refresh_root(request, clock);
    REQUIRE_FALSE(result.attempted);
    REQUIRE(result.exit_code == 5);
    REQUIRE(fileweaver::catalog_status(fixture.path()).scan_records == count);
    request.root_id = 99999;
    REQUIRE_THROWS_AS(fileweaver::refresh_root(request, clock), fileweaver::StorageError);
    request.root_id = 0;
    REQUIRE_THROWS_AS(fileweaver::refresh_root(request, clock), fileweaver::StorageError);
}

TEST_CASE("Glob matching is bounded Unicode-aware and case-sensitive") {
    using fileweaver::GlobMatcher;
    REQUIRE(GlobMatcher("*.cpp").matches("日本😀.cpp"));
    REQUIRE_FALSE(GlobMatcher("*.cpp").matches("UPPER.CPP"));
    REQUIRE(GlobMatcher("報告?.txt").matches("報告😀.txt"));
    REQUIRE(GlobMatcher("[a-c]*").matches("beta"));
    REQUIRE(GlobMatcher("[!a-c]*").matches("日本"));
    REQUIRE(GlobMatcher("a[[]1].txt").matches("a[1].txt"));
    REQUIRE(GlobMatcher("[]]").matches("]"));
    REQUIRE(GlobMatcher("*ab*cd?").matches("zzabxxcd1"));
    REQUIRE_FALSE(GlobMatcher("*ab*cd?").matches("zzabxxcd"));
    REQUIRE_THROWS_AS(GlobMatcher("[z-a]"), fileweaver::StorageError);
    REQUIRE_THROWS_AS(GlobMatcher("[abc"), fileweaver::StorageError);
    REQUIRE_THROWS_AS(GlobMatcher(std::string(257, '*')), fileweaver::StorageError);
    std::stop_source stopped;
    stopped.request_stop();
    REQUIRE_THROWS_AS(GlobMatcher("*").matches("name", stopped.get_token()),
                      fileweaver::StorageError);
    const auto hard = std::string(200, 'a') + "b";
    REQUIRE_FALSE(GlobMatcher("*a*a*a*a*a*b").matches(hard + "c"));
}
TEST_CASE("Fuzzy path tokens match components without crossing separators") {
    const fileweaver::FuzzyMatcher matcher("img proc fin");
    REQUIRE_FALSE(matcher.candidate("final.txt"));
    REQUIRE(matcher.candidate("final.txt", {}, "C:/image_processing/final.txt"));
    REQUIRE(matcher.score("final.txt", {}, "C:/image_processing/final.txt").has_value());
    REQUIRE_FALSE(fileweaver::FuzzyMatcher("ab").candidate("z.txt", {}, "C:/a/b/z.txt"));
    const auto own = fileweaver::FuzzyMatcher("report").score("report.txt");
    const auto parent =
        fileweaver::FuzzyMatcher("report").score("other.txt", {}, "C:/report/other.txt");
    REQUIRE(own.has_value());
    REQUIRE(parent.has_value());
    if (own && parent) {
        REQUIRE(*own > *parent);
    }
}
