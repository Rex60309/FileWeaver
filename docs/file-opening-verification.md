# Confirmed Windows file opening verification

Implementation date: 2026-11-09 (Asia/Taipei). Scope: the explicitly approved
Phase 2 Windows slice: supported common document/image/media files only, a full
path confirmation per operation, and no program/script/shortcut/other-type opening.
No release packaging or additional Linux implementation/manual verification.
ADR-0029 records the safety boundary, external-handler limitations and alternatives.

## Implementation

file_open.* contains the lexical allowlist, guarded component-by-component Win32
metadata inspection, COM STA/ShellExecuteExW adapter and one bounded background
worker. At most one confirmed request is accepted; there is no waiting queue or
automatic retry. Search keeps its existing independent worker and SQLite use case.
No schema, migration, index-writing, CLI, telemetry or clipboard-history change.
Shell32 and Ole32 are Windows system libraries, not new third-party packages.

search_window.* uses the same action for the button, result Enter and double-click.
The plain-text scrollable confirmation displays an immutable full path snapshot;
Cancel is the default and pressing Enter on that dialog cancels. Mere selection,
query input, a changed result or cancellation does not launch anything. Query
Enter still searches. The worker verifies current existence/type and rejects
remote/offline/reparse paths before sending a Unicode open request with no shell
command string or arguments. OS security checks/prompts remain enabled.

External viewers can process active content, write files or use a network; a file
extension allowlist is not malware/content scanning or a sandbox. FileWeaver does
not read content or modify source bytes. Metadata inspection and path-based shell
handoff are not atomic; an adversarial replacement race remains. Once handoff
starts it cannot be recalled, and OS/COM/handler calls can delay shutdown. No hard
cancellation deadline or guaranteed successful document display is promised.

## Tests and code review

Generated temporary fixtures only. Automated launch callbacks are fake: no real
external application is started by CTest. Tests cover:

- Unicode/spaces/punctuation and case-insensitive allowed extension handling.
- Executable/script/shortcut/archive/macro-enabled, URL/UNC/device/relative/ADS,
  invalid UTF-16, direction-control, ambiguous component and reserved-name rejection.
- Cancellation before/during inspection without calling the launcher; inspection
  and launcher errors; exception propagation to a visible failure result.
- Native current-file metadata, missing target, file replaced with a directory,
  and a real junction ancestor; rejected targets never reach the fake launcher.
- No implicit opening, complete confirmed Unicode path, default-Enter cancellation,
  Cancel button, result Enter and preserving the confirmed path if selection changes.
- One-operation backpressure, GUI event-loop heartbeat, explicit cancellation and
  cancellation/join when the controller is destroyed while active.

The existing synthetic junction helper was extracted into tests/support/directory_link.hpp
for reuse by Catch2 storage tests and Qt tests. It retains the native junction and
POSIX symlink behavior, reports failures via exceptions rather than skips, and
refuses to repurpose an existing destination. This extraction adds no product API.

## Executed Windows gates

Windows GCC 13.2 x64, Qt 6.11.3, C++20, CMake 3.29.2, Ninja, pinned vcpkg.
Tests use owned build/test-temp (TEMP/TMP); existing link fixtures need permissions
for junction/hard-link creation. Exact project commands executed:

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

All configure/build/format/lint commands passed. Core Debug/Release: 11/11 CTest
tests each. Desktop Debug/Release: 12/12 each, with Qt reports showing 15 passes
(13 functions plus init/cleanup), zero failed/skipped/blacklisted. clang-format
23.1.5 and clang-tidy 23.1.4 were used. Initial enum-size diagnostics were corrected
with an explicit uint8_t underlying type; no check or test was disabled.

Windows combined ASan/UBSan is unsupported by the configured preset; no sanitizer
coverage of the Windows adapter is claimed. No additional Linux run was scheduled,
as requested. Existing hosted jobs are separate and their actual results must be
recorded before claiming hosted acceptance. No performance measurements claimed.

## Native Windows observation

An owned temporary root contained a synthetic UTF-8 text file, a separate source
folder, and an explicitly initialized/indexed SQLite database. No real user files
were indexed or opened. Observed: Unicode index selection, one synthetic result,
Open button state, full Unicode path in a scrollable confirmation, Enter cancelling
back to search, double-click requiring confirmation, then explicit Open handing
the synthetic file to the existing Windows Notepad association. FileWeaver reported
shell handoff and the matching synthetic-document window appeared. Only that
unmodified synthetic document tab was closed; pre-existing editor tabs were left
alone. The FileWeaver test window was then closed. No private source path or raw
observation/log/database was committed; generated evidence stays under build/.

## Remaining scope

Hosted Windows/MSVC acceptance is pending push. Other file types, containing-folder
opening, user-selected handlers, persistent preferences, fuzzy ranking, watcher,
previews and release packaging are outside this slice. Next proposed milestone:
explicitly selected containing-folder display in Explorer, after scope approval.
