# Search filter verification

Date: 2026-11-08
Scope: Phase 1 extension, inclusive-size and exact-root filters.
Environment: Windows GCC 13.2.0; existing vcpkg dependencies and schema v2.
Decision: ADR-0007-search-filters.md. No dependency/schema changes.

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed with existing compiler/vcpkg configuration |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 2/2 including CLI filter E2E |
| `ctest --preset debug --output-on-failure -R cli-contract` | Passed, 1/1 |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 38 cases / 210 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed; no project warnings/errors |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 2/2 |
| `build/debug/fileweaver.exe search --help` | Passed; filter options displayed |

## Acceptance evidence

Tests exercise dotted/undotted and ASCII-cased extensions, final archive suffix,
empty-extension metadata, zero-byte files, inclusive lower/upper size boundaries,
maximum signed 64-bit bounds, exact-mode combinations, AND composition, and
limit-after-filter/order. SQL-like extension input matches literally.

Multiple roots, exact membership (not parent-prefix), relative spelling, trailing
dot normalization, removed/offline roots and unregistered root results are tested.
Invalid negative/reversed sizes, separator/NUL/oversized extensions and empty/NUL
root inputs are rejected. CLI tests cover successful combined filters, no matches,
reversed range, negative size, unsupported size units, integer overflow and missing
values for all four filter options. Diagnostics go to stderr with exit 2.

The initial fixture used two filenames differing only by case in one directory;
Windows merged them and an expectation failed. The fixture was corrected to put
them in separate synthetic subdirectories, preserving both casing assertions.
No tests or validation were weakened or skipped.

## Safety and limits

Tests use synthetic temporary databases/source directories only. Full-suite runs
use authorized permissions for the existing native Windows junction fixtures.
Search remains read-only SQL; root filtering does not stat/scan source paths.
No source-file changes, real-user indexing, network access or collection occurred.
Existing cancellation, result budgets, ordering and stale-index semantics remain.

Root aliases/case variants are not universally unified. Extension case folding
is ASCII-only. Filters use indexed values, so stale records may remain. No time
filters, custom sort, content search, JSON or new index structures are introduced.
Optional predicates may affect query plans; no performance gain is claimed.
CI, supported-platform ASan/UBSan, ACL and concurrency stress remain unexecuted.
No commit/release is claimed. Next: modification-time filtering and explicit sort,
or a separately specified JSON contract, each with tests and README updates.
