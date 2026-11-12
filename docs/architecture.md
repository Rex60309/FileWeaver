# Architecture

FileWeaver indexes file metadata and searches it through a Windows Qt interface
or the CLI. Both frontends call the same C++20 indexing and search code.

## Code layout

| Location | What it contains |
| --- | --- |
| apps/cli | Commands, Unicode console output and JSON formatting |
| apps/desktop | Qt UI, background workers, Windows hotkey/watcher and preview processes |
| include/fileweaver/application | Requests shared by indexing, refresh, inspection and search |
| src/core | Fuzzy ranking, glob matching, text helpers and watcher scheduling |
| src/filesystem | Folder scanning, cancellation and skipped-file/error reporting |
| src/database | SQLite schema, transactions, indexing and parameter-bound queries |
| tests, benchmarks | Tests with generated files and scripts for local measurements |

## Indexing and search

The scanner reads metadata from folders the user selects. SQLite stores file
records, root folders, timestamps and scan settings. A rescan uses the saved
settings. If a scan is cancelled or cannot finish, the previous committed file
records remain available. Exclusions or skipped paths can prevent a full deletion
check, so some old results may stay in the index until they can be checked safely.

Search supports filename/path matching, type/size/root/time filters and a fixed
sort order for ties. Type filters run before the result limit is applied. Fuzzy
search limits the number of candidates and scores them with configurable weights.
Queries bind user values as SQL parameters. B-tree indexes help exact matching;
substring matching still checks candidate rows individually.

## Background work and previews

Search, indexing, watcher updates and preview requests run on background workers
with cancellation and bounded queues. Each SQLite operation owns its connection
on its worker thread. The UI allows either its indexer or watcher to write at a
time. When notifications overflow or the watcher restarts, it rescans to catch
changes that may have been missed.

Users turn previews on manually. A separate helper process parses the selected
file, with limits on time, memory and response size. Text previews read up to
64 KiB/2000 lines. Image and PDF inputs are limited to 8 MiB, with thumbnails up
to 256x256. The UI keeps a small, bounded thumbnail cache in memory. The helper
has resource limits, but it still runs with the user's permissions. FileWeaver
does not change source files, and opening a file externally requires confirmation.

## Tests and dependencies

CMake presets cover core/desktop Debug and Release builds. The sanitizer preset
uses Clang/GCC on supported non-Windows platforms. Tests create temporary files to
check Unicode paths, indexing, search, cancellation, watchers and damaged or
oversized previews. GitHub Actions is currently paused.

The [performance reports](performance/) list the source revision, hardware, cache
conditions and what each timer includes. Their results apply to those runs; rerun
the scripts before quoting performance for a newer build. The
[dependency notes](../ports/README.md) explain the Poppler integration and its
redistribution requirements.
