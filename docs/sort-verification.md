# Search sorting verification

Date: 2026-11-08
Scope: Phase 1 deterministic name/size/modified ordering and descending primary.
Environment: Windows GCC 13.2.0; existing schema v2 and dependencies unchanged.
Decision: ADR-0009-search-sorting.md.

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed with existing compiler/vcpkg configuration |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 2/2 |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 42 cases / 265 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed; deliberate invalid-enum test has scoped annotation |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 2/2 |
| `build/debug/fileweaver.exe search --help` | Passed; sort and direction options displayed |

## Acceptance evidence

Synthetic indexed records verify default name ascending, name descending, size
ascending/descending, modification-time ascending/descending, ascending tie-breaks
under descending size order, filtering before LIMIT, time-filter composition and
invalid API enum rejection. Existing deterministic filename/path tie tests remain.
CLI E2E exercises every sort with descending/limit/extension filters and rejects
unknown, SQL-like and empty sort values with exit 2 and stderr diagnostics.
The deliberate fixed-underlying invalid-enum test initially triggered the enum
analyzer. A single-line NOLINT for that test cast documents its intent; production
validation/checks and the rejection assertion are retained unchanged.
All fixtures are generated temporary metadata/files. Full suite uses authorized
junction-creation permissions; no tests were skipped.

## Safety and limits

The SQL sort expression is selected exclusively from internal constants, not user
text. Search remains read-only with bound query/filter values. Source files are
not read or mutated. No schema/dependency change, download or runtime network
behavior was introduced. Primary descending does not reverse tie-breaks. Default
order and human result format remain compatible with the prior slice.

Sorting is by indexed logical bytes/time and binary names, not live source state,
locale collation or relevance. SQLite may use temporary sort storage; application
result budgets do not cap all SQLite working memory. No benchmark, concurrency
stress, CI or supported-platform ASan/UBSan execution is claimed. No commit/release
was made. Next smallest slice: explicitly specified JSON output contract and tests.
