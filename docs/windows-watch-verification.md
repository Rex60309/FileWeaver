# Opt-in Windows single-root watch verification

Date: 2026-11-10 (Asia/Taipei). Authorized Phase 2 scope and threading/safety model
are recorded in ADR-0032. User explicitly approved retaining conservative Missing
reconciliation for roots with exclusions/skipped entries and showing that limitation.
No CLI watch command, multi-root service, packaging or new manual Linux work.

## Implementation

core/watch_schedule.hpp is a monotonic, caller-clocked coalescing/retry/periodic
schedule. application/refresh.hpp adds RootRefreshRequest/refresh_root; the storage
adapter shares the existing refresh planner, selects one bound registration ID and
rejects a changed expected path before scanning. Existing refresh-all behavior,
schema v3, saved policies, SQLite WAL/connection ownership/transactions, scanner
exclusions, Missing guard and rollback remain. No migration or package added.

desktop/directory_changes.* owns one 32 KiB DWORD-aligned overlapped notification,
completion/cancellation handles, rearming and CancelIoEx/drain lifetime. It does not
parse/store raw event names. It validates local roots/ancestors and database
placement/identity before attaching and before every sync. Database/sidecars must
be outside the watched root; hardlinks/reparse aliases are rejected. Guarded path
checks do not eliminate malicious TOCTOU replacement. Some special storage drivers
may not provide the required final-path metadata and are refused.

desktop/watch_controller.* has one persistent worker, one pending command and one
latest reply. It serializes root inspection/notifications/index writing. Search
keeps its existing independent reader/worker. Generation rejection prevents stale
messages after switching databases. Monotonic/UTC clocks and adapters are injected.
Every attachment reconciles; events coalesce for 500 ms up to 2 s, zero-byte/
ERROR_NOTIFY_ENUM_DIR triggers immediate reconciliation, errors retry attachment in
5 s and periodic reconciliation runs 60 s after the last successful scan. Native
poll normally waits <=200 ms. No perfect event capture/update-delay claim.

The UI exposes Load roots/select/Enable/Stop and last success UTC timestamp. No
catalog read or source scan happens merely by launching/selecting an index. A
committed scan reruns the current search. Startup is off, no persisted settings,
tray/service or startup registration. Minimizing retains the explicit session;
switching index/closing cancels it. Unknown-policy roots cannot be enabled.

No source contents/files are changed, clipboard/access history collected or
application network behavior added. Authorized writes affect only the selected
index/its existing scan history. History can grow with periodic scans; retention
is unchanged. Completed commits are not undone by stopping. Busy SQLite/filesystem
calls or driver cancellation can delay close; buffers stay alive until completion.

## Acceptance tests

All fixtures are generated temporary directories/databases. Core schedule tests
use injected times for startup, debounce/max deferral, silent-gap period, urgent
overflow and retry boundaries. Single-root tests verify persisted exclusions,
unchanged other roots, conservative stale retention, policy disappearance,
registration/path mismatch before writing and pre-cancellation.

Qt tests call actual Windows notification APIs on synthetic Unicode filenames
for create/modify/rename/delete, cancellation, junction rejection, database inside
root and hardlink alias rejection. Native notification probes latch observations
because QTRY may evaluate predicates more than once; consuming an event twice was
a test bug corrected without weakening the required notification assertion.
Real worker integration initializes an isolated index, checks create/size/rename/
delete, changes saved exclusions and verifies the Missing warning, stops, modifies
fixtures while stopped and restarts to reconcile. Fake adapters inject overflow,
disconnection, failed reconciliation, silent gaps, cancellation during a scan and
destruction/join. UI tests verify no implicit work, manual opt-in and database
switch invalidation. No external program, user folder or native clipboard is used.

Initial focused schedule/selected-root cases passed 32 assertions (31 after
reusing the existing checked optional-index helper instead of a separate REQUIRE); initial Qt
notification/reconciliation/opt-in tests passed. A test initially expected Missing
updates despite saved exclusions; repository ADR-0005 forbids that. The user chose
to retain the guard, and tests now explicitly assert stale retention and warning.
The async Missing-warning check also waits for the specific new-policy status,
not merely any older queued scan counter. Quality gates/final evidence follow.

## Commands and platform limits

Tests set TEMP/TMP to build/test-temp. Use repository presets/targets:

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
git diff --check
```

Combined ASan/UBSan is unsupported by the configured Windows toolchain; do not
claim native watcher sanitizer coverage. Existing hosted core analysis can cover
portable schedule/selected-root code. No native UI screenshot or controlled
throughput/delay benchmark is claimed. Defaults are configuration, not measurements.
Local final results (GCC 13.2 / Qt 6.11.3, C++20):

| Gate | Observed result |
| --- | --- |
| Core Debug / Release full CTest | 12/12 each, 58.85 / 57.37 seconds |
| Desktop Debug / Release full CTest | 13/13 each, 66.83 / 62.34 seconds |
| Qt interaction cases | 23 passes, zero failed/skipped/blacklisted in both configurations |
| Core and desktop Debug format-check/lint | Passed |
| All four configure/build presets | Passed |
| Final focused core schedule/selected-root | 31 assertions, two cases passed |
| Release storage after checked-helper cleanup | Passed, 1.18 seconds |
| git diff --check | Passed |

Minimal Qt emits expected size-hint/raise warnings; none were hidden. Lint first
caught unchecked optional access in a test assertion (fixed using the existing
checked helper), a pending-job access (added an explicit guard), signed Win32 flag
operands and ignored nodiscard results in exception tests. All were corrected
without disabling checks. One Debug async assertion initially saw an older queued
success after policy change; it now waits for the specific Missing-warning status.
The complete Debug suite subsequently passed. No failing test was skipped.

Hosted Windows acceptance for this revision has not yet been recorded.

## Remaining scope

Path-component search, bounded safe previews, multiple roots/CLI watch, special
storage support and more selective safe Missing reconciliation remain separate
work. The current slice deliberately keeps the established conservative behavior.

## Primary API contracts

- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew
