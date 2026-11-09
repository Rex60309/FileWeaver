# Read-only catalog inspection verification

Date: 2026-11-08
Scope: Phase 1 roots/status APIs, human output and JSON contracts.
Environment: Windows GCC 13.2.0 / Python 3.13.9, existing schema/dependencies.
Decision: ADR-0012-catalog-inspection.md.

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed with existing compiler/vcpkg configuration |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 3/3 |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 54 cases / 383 assertions |
| `build/debug/storage_tests.exe 'Catalog*,Root inventory*' --reporter compact` | Passed, 4 cases / 49 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 3/3 |
| `build/debug/fileweaver.exe roots --help` | Passed |
| `build/debug/fileweaver.exe status --help` | Passed |

## Acceptance evidence

Tests cover empty catalogs, deterministic root ordering, exact saved exclusions/
depth, optional scan time, recorded status and correct present/missing/scan/running
history counts. Deleting the synthetic source does not change inspection counts
or cause a scan. Queries add no scan history records. V2 inspection returns unknown
policies/null depth without migration; an explicit version assertion verifies v2
is preserved. Missing storage is not created, unsupported versions/cancellation
fail, NUL paths are rejected, root lists >10000 fail while status counts all roots.

Strict JSON E2E checks empty/populated roots/status, saved exclusions, nullable v2
policies, integer timestamps, global flags, missing argument/storage and malformed
UTF-8 root output without partial JSON. Human E2E covers both commands. All
fixtures are isolated generated metadata/files. Authorized permissions are used
for existing junction tests, not real-user data. No tests are skipped.

## Safety and limits

Open existing v2/v3 storage read-only; no init/migration, source stat/content read,
policy update, refresh, network behavior or new dependency. SQLite may need its
own WAL/SHM sidecars. Counts can be stale and include duplicates across roots.
Recorded running scans do not prove a live process. This is inspection, not
integrity checking or crash-history repair.

Root text/count limits bound application results, not all SQLite working memory.
Count queries can scan large tables. No performance, concurrent-reader, ACL,
in-flight cancellation or aggregate text-budget stress is claimed. Pre-requested
cancellation, invalid/capped catalogs and compatibility are exercised. Existing
alias/invalid-UTF8/stale-index limits apply. CI/supported-platform ASan/UBSan remain
pending. No commit/release is claimed. Next: reproducible search benchmark with
correctness and raw environment/timing evidence, then remaining Phase 1 gates.
