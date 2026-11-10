# Confirmed Windows Explorer reveal verification

Date: 2026-11-10 (Asia/Taipei). Authorized Phase 2 Windows slice: full-path
confirmation before locating any extension, selection only, existing opening
allowlist unchanged. See ADR-0030. No release packaging or added Linux work.

## Changes and safety

apps/desktop/file_open.* shares local-path/metadata inspection, adds the Windows
SHParseDisplayName/SHOpenFolderAndSelectItems adapter and typed action dispatch on
the existing single worker. apps/desktop/search_window.* adds the location button
and result Ctrl+Enter, immutable full-path confirmation and Cancel default.
No database schema, core CLI, dependency manifest, file writes, clipboard collection,
telemetry or network behavior changes. No command string or file association is
used for reveal. Windows Explorer may load its own extensions/previews; this is
not a sandbox. Inspection and handoff are not atomic. Shell calls may block and
cannot be recalled; acceptance does not prove foreground display.

## Automated acceptance

tests/desktop/search_window_test.cpp adds reveal-only suffix acceptance, invalid
paths, cancellation before/during inspection, inspection/handoff errors, button
and Ctrl+Enter confirmation, default cancellation, unchanged opening rejection,
immutable path capture and wrong-action prevention. Shared-worker tests cover both
open-blocks-reveal and reveal-blocks-open; existing exception/destruction/heartbeat
checks remain. Native metadata tests use generated Unicode files, missing-file and
directory replacement, plus existing junction rejection. All CTest handoffs are
fake: they cannot open Explorer or launch files. Minimal Qt clipboard remains
process-local. No real user data is used.

## Native observation

Windows native Qt Debug was exercised against a dedicated generated temporary
index and an inert plaintext file named FileWeaver_定位_日本語😀.exe (39 bytes,
not an executable). The location button displayed its full Unicode path; Enter
cancelled and no Explorer window appeared. Result Ctrl+Enter displayed confirmation
again. Explicit Show location opened the synthetic parent folder in Explorer;
the item name matched and Explorer reported one selected item, 39 bytes. FileWeaver
reported Shell acceptance. Only the owned Explorer and FileWeaver test windows
were closed; no file was executed, renamed or deleted. No raw screenshot, private
path, test database or log is committed. The first Explorer capture needed an app
approval retry; it succeeded on retry. Foreground activation was used for observation,
so this is not a claim that the reveal API always brings Explorer forward.

## Quality gates

Local Windows GCC 13.2 / Qt 6.11.3, C++20, existing repository presets.
Core Debug/Release configure/build and CTest passed (11/11 each, 58.47/55.55 seconds).
Core and desktop Debug format-check and lint passed. Desktop Debug/Release
configure/build and CTest passed (12/12 each); Qt interaction reports 17 passes,
0 failures, 0 skips and 0 blacklisted tests. Minimal Qt emits expected size-hint
plugin warnings. Final byte comparison confirmed the 39-byte native fixture
remained unchanged.
One performance-no-automatic-move lint error was corrected without suppressing it.
Windows combined ASan/UBSan is unsupported by the configured toolchain; no new
sanitizer result or manual Linux coverage is claimed.

Exact commands (TEMP/TMP set to the repository build/test-temp for tests):

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

## Remaining work

Hosted Windows acceptance has not yet been recorded for this revision. Fuzzy
ranking, watcher/reconciliation and safe previews remain planned Phase 2 work.
The next proposed smallest milestone is deterministic fuzzy-search ranking;
its public search behavior requires a separate scope decision.

## Hosted Windows status

Implementation revision: `76dcff04bec37b653c9a3777aa6d94ef9311e956`.
[Quality run 42764008690](https://github.com/Rex60309/FileWeaver/actions/runs/42764008690)
passed its Windows core job. The first desktop attempt failed before compilation
because the GitHub metadata API returned HTTP 403 (rate limit exceeded).
After the other jobs finished, the desktop job was rerun without changing code,
checksums, download URLs or verification rules. Attempt 2 passed metadata
preparation and entered desktop Debug dependency configuration/build; its final
result is pending. This is not hosted desktop acceptance or a release claim.
The existing analysis job also passed, but no additional manual Linux work was
performed. This follow-up is Markdown only and uses [skip ci]; application source
and tests remain the implementation revision linked above.
