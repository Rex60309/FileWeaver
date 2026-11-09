# Filename search verification

Date: 2026-11-08
Scope: Phase 1 exact/substring filename search and human CLI output.
Environment: Windows GCC 13.2.0; existing dependencies and schema v2 unchanged.
Decision: ADR-0006-filename-search.md.

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed with existing compiler/vcpkg configuration |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 2/2 including CLI search E2E |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 35 cases / 183 assertions |
| `build/debug/storage_tests.exe 'Search*' --reporter compact` | Passed, 7 cases / 38 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed; initial findings fixed without disabling checks |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 2/2 |
| `build/debug/fileweaver.exe search --help` | Passed; mode, limit, query and database options displayed |

All files/databases were synthetic temporary fixtures. Full-suite tests ran with
authorized junction-creation permissions, without skipping existing tests.

## Acceptance evidence

Exact case-sensitive matching, ASCII-insensitive substring matching, Unicode
literal queries, percent/underscore literals and SQL-like injection text are
tested. Deterministic tie ordering and limit-after-sort are verified. A deleted
source remains searchable before re-indexing, then disappears from results once
marked Missing. No live source read is needed by search.

Input checks cover empty/NUL/oversized query, zero/excessive limits, absent index,
unsupported schema, empty result and pre-requested cancellation. Per-field and
aggregate text limits are exercised with synthetic stored metadata; exceeding
the budget throws rather than returning partial results. Presentation helper tests
verify quote/backslash, newline/tab/ESC/DEL escaping and Unicode preservation.
CLI E2E covers exact/substring success, no-match empty stdout, invalid limits
(including negative input), unsupported mode and uninitialized storage exit 4.

## Safety and limits

Search uses a read-only SQLite connection/read transaction and bound parameters;
it does not scan roots, initialize storage, alter schema or read source contents.
SQLite may require access to its WAL/SHM sidecars. No dependencies, network
downloads, telemetry or file mutation features were introduced. Query text is
not persisted. Results contain full paths from the local index.

Result count/text budgets bound returned application data, not all SQLite query
working memory. No large-index performance benchmark, concurrent-reader stress,
permission ACL fixture or in-flight cancellation stress test was executed.
SQLite progress cancellation is implemented; pre-requested cancellation is the
executed test. ASCII-only case folding, stale-index records and overlapping-root
duplicates remain documented limitations. Exact matching can use the name B-tree;
arbitrary substring queries may scan the index. No latency claim is made.

CI and supported-platform ASan/UBSan remain pending; no commit/release is claimed.
Next smallest slice: extension/size/root filters with deterministic search tests,
followed by a separately specified JSON output contract and reproducible benchmark.
