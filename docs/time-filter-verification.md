# Modification-time filter verification

Date: 2026-11-08
Scope: Phase 1 strict UTC modification-time filters and pure core parser.
Environment: Windows GCC 13.2.0; existing dependencies/schema v2 unchanged.
Decision: ADR-0008-modification-time-filters.md.

## Executed checks

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed with existing compiler/vcpkg configuration |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 2/2 |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 41 cases / 249 assertions |
| `build/debug/storage_tests.exe 'UTC*,Search modification*' --reporter compact` | Passed, 3 cases / 39 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed after explicit int64 arithmetic correction |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 2/2 |
| `build/debug/fileweaver.exe search --help` | Passed; both UTC bound options displayed |

Full-suite tests ran with authorized permissions for synthetic junction fixtures.
No test was skipped. No real user database was searched/updated/indexed.

## Acceptance evidence

Parser assertions cover Unix epoch, one second before epoch, Gregorian leap years,
the earliest/latest representable whole-second boundaries, invalid leap days,
month/day/hour/minute/second ranges, leap-second rejection, embedded NUL, missing
timezone, offset/fraction/lowercase syntax and out-of-storage-range timestamps.
All parsing is independent of timezone, locale and current wall clock.

Synthetic indexed timestamps -1, 0 and +1 nanoseconds verify strictly exclusive
after/before predicates. Tests combine time bounds with extension, inclusive size,
exact root and limit. Equal/reversed paired bounds fail; signed-int64 API endpoint
bounds work without +/-1 arithmetic or overflow. CLI E2E exercises broad valid
UTC bounds, other-filter combinations, invalid calendar/syntax/range, reversed
times and missing values, with exit 2 and stderr diagnostics for bad arguments.

## Safety and remaining limitations

New parser is in a portable core target with no SQLite/presentation dependency.
Search remains read-only SQL with bound parameters. No schema migration, dependency
download, network behavior, source content reads or file mutation was introduced.
Tests use synthetic metadata/temporary files only. Time is modification metadata,
not creation/access/activity. Existing stale-index, duplicate-root and cancellation
limitations apply. CLI accepts whole UTC seconds only; no local/relative/fractional
syntax, timezone offsets or custom sorting.

Initial lint identified implicit int-to-int64 widening after hour/minute products;
calculation was changed to multiply explicit int64 operands. No checks were disabled.
CI and supported-platform ASan/UBSan remain pending, as do performance/concurrency
stress and in-flight cancellation stress. No commit or release is claimed.
Next smallest slice: explicit deterministic sorting, then a specified JSON contract.
