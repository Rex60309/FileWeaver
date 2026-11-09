# Windows desktop search verification

Date: 2026-11-09. Scope: the authorized Phase 2 Windows search-window slice,
not completion of Phase 2 or a formal release. ADR-0027 records authorization.

## Behavior and safety

Optional Qt Widgets UI reuses search_files and existing read-only connections.
No schema, migration, CLI JSON or search semantics changes. A single worker,
one pending request, one staged reply and generation rejection bound queued
work. Typing cancels old work; shutdown requests stop and joins. Blocking OS
operations can delay shutdown, so no fixed cancellation latency is promised.

Only explicitly chosen existing indexes are searched. Database/query selection
alone does not scan, initialize an index, stat result paths or open source files.
No watcher, global hotkey, file launching, preview, telemetry or content indexing.
Clipboard writes require a user copy action; no history is collected.

## Local Windows evidence

Windows, GCC 13.2 x64 (Strawberry), C++20, CMake 3.29.2, Ninja, Qt 6.11.3,
pinned vcpkg ec48c11eae9448c8c6e7739d93e06fa2433aa95f.
Qt dependencies were explicitly downloaded and built in Debug and Release;
core dependencies/default presets remain independent of Qt.

Executed standard commands:

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel
ctest --preset desktop-debug --output-on-failure
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel
ctest --preset desktop-release --output-on-failure
```

CLI Debug/Release: 11/11 CTest tests passed each. Desktop Debug/Release: 12/12
passed each. The desktop interaction report has 8 passes, 0 failures, 0 skipped,
0 blacklisted (six test functions plus Qt init/cleanup). Desktop format and
lint passed with clang-format 23.1.5 / clang-tidy 23.1.4. Default CLI format and lint also passed. No new performance measurements are claimed.

Initial test execution was blocked by the restricted temporary directory and
junction/hard-link creation permissions. Reruns used an owned build/test-temp
(TEMP/TMP) and permission to run the unchanged synthetic fixture tests. No
assertions were removed or skipped. Initial Qt dependency compilation selected
an incompatible 32-bit windres; using the matching x64 compiler/resource tools
resolved it. Intermediate compile/lint failures were corrected before acceptance.
Qt-generated moc headers are excluded from header diagnostics; handwritten
headers and all project checks remain enabled.

Qt tests force the minimal platform, which uses a process-local clipboard.
They cover Unicode database names/results, all three modes, keyboard navigation,
button/shortcut copy, no implicit search, invalid arguments, missing DB without
creation, offline source snapshots, bounded latest-request replacement, stale
success rejection, event-loop responsiveness, cancellation/join, invalid UTF-8.
CTest uses an exclusive generated report and fails on nonzero exit, missing
report, failed/skipped/blacklisted tests. Reports stay under the ignored build tree.

Native Windows inspection separately used the windows platform with the modern
Windows style, an owned synthetic three-file index, and no real indexed user data.
Observed: index chooser accepts a Unicode database path; searching 報告 returns
two rows including 報告_日本語😀.txt; Down enters results, Down changes selection;
Ctrl+C reports native clipboard ownership success for the selected full path;
Escape closes the window and the app exits. Original clipboard contents were
not read. Window headings, controls, empty state, result rows and path preview
were inspected at the default window size. This is not screen-reader, all-DPI,
all-theme or all-Windows-version certification.

## Additional gates and limits

Windows combined ASan/UBSan is unsupported by the configured preset; no GUI
sanitizer coverage is claimed. Core Linux ASan/UBSan rerun passed 11/11 CTest tests (239.43 seconds), with
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 and
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. The existing WSL build uses
GCC and Unix Makefiles; initial preset generator mismatch was corrected by
explicitly retaining that generator. WSL reported filesystem clock-skew warnings;
no core source changed in this slice. Hosted MSVC CI results are pending push. The hosted desktop job runs
Debug/Release with the same synthetic minimal-platform interaction suite;
it does not substitute for native visual inspection.

No distributable ZIP/installer is produced by this milestone. Runtime DLLs and
platform/style plugins must accompany the executable. Clean-checkout MSVC CI,
when successful, verifies building/testing rather than deployment to a clean PC.
The rest of Phase 2 remains planned and requires a separately selected milestone.

## Ignore-rule audit

Added SQLite rollback journals and .db sidecars, local .env files (template
exceptions), alternate root build-* trees and in-source CMake artifacts. Verified
with git check-ignore that examples of these and normal build DLLs/reports are
ignored, while tests/e2e/json_contract.py and .env.example remain visible.
git ls-files found no tracked exe/dll/obj/sqlite/db/log/pyc/.env artifacts.
These rules cannot prevent deliberately forced additions or remove tracked files;
review remains required. No user file or database was modified by this audit.

Executed sanitizer commands inside the existing Ubuntu environment:

```sh
cmake --preset sanitizers -G "Unix Makefiles" -B build/linux/sanitizers -DCMAKE_PREFIX_PATH="$PWD/build/linux/dependencies"
cmake --build build/linux/sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/linux/sanitizers --output-on-failure
```
