# Failed/incomplete index retention and recovery — 2026-11-09

Scope: authorized Phase 1 correctness validation of existing per-root rollback,
partial refresh and recovery. No production C++/API/schema/dependency behavior
change. ADR-0022 records deterministic fault selection and test-only SQL boundary.

## Implementation and acceptance

- benchmarks/search/verify_failure_recovery.py: six phases, 124 filter cases each,
  full ID snapshots and roots/status checks, deterministic metadata write failure,
  separate incomplete-return path, healthy-root partial commit and final recovery.
  Reuses existing fixture/oracle/mutation guards. Product data is read through CLI
  JSON, not SQL. Direct SQL is confined to fault objects/counters, preconditioned
  depth updates and final schema metadata in its own new temporary database.
- tests/e2e/failure_recovery_contract.py: minimum three files per root, all six
  target-result/state transitions, no-overwrite and rejected 2/1025 bounds. It also
  creates a separate owned sentinel database and proves the fault helper refuses
  outside access without changing its version; missing databases are not created.
- CMake registers the new E2E; README/status/ADR/evidence updated. No product code
  or existing database migration was added.

## Exact quality commands and results

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure -R failure-recovery-contract
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset debug --output-on-failure
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Focused test passed 1/1. Complete Debug and Release CTest each passed 10/10 groups;
configure/build/format/lint passed without new suppressions. C++ sources were
unchanged. Static analysis covers configured C++; new Python behavior is verified
by actual E2E, not claimed analyzed by clang-tidy. Preserve/exclude unchanged
supplied policy/spec trailing spaces in scoped whitespace checks.

Linux/Clang CI and ASan/UBSan execution remain pending; the combined sanitizer
configuration excludes this Windows environment. No sanitizer, cross-platform
runtime, clean-checkout release acceptance or commit is claimed.

## Recorded generated runs

```powershell
python benchmarks/search/verify_failure_recovery.py --cli build/debug/fileweaver.exe --build-info build/debug/benchmark-build-info-Debug.json --per-root 3 --output build/validation/failure-recovery-debug-min-20261109.json
python benchmarks/search/verify_failure_recovery.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --per-root 24 --output build/validation/failure-recovery-release-20261109.json
```

Both succeeded: 744 searches, three checked failure paths, eight returned scan
summaries and six catalog/identity snapshots. There are ten scan-history records:
two exception failures intentionally have no misleading index-summary JSON.
Release report: 329522 bytes, created 2026-11-08 16:40:57 UTC (2026-11-09 local),
48 initial files/two roots, kind failure_recovery_correctness_only_not_performance.
Raw reports remain ignored in build; no timing/performance claims are made.

| Phase | Present | Missing | Total records | Scan history | Root A state | A depth |
| --- | --- | --- | --- | --- | --- | --- |
| initial | 48 | 0 | 48 | 2 | completed | 128 |
| failed_index | 48 | 0 | 48 | 3 | failed | 128 |
| partial_refresh | 48 | 0 | 48 | 5 | failed | 128 |
| write_recovery | 48 | 1 | 49 | 6 | completed | 128 |
| incomplete_refresh | 48 | 1 | 49 | 8 | incomplete | 1 |
| depth_recovery | 49 | 1 | 50 | 10 | completed | 128 |

### Write exception path

A static trigger in the fixture database permits one changed-row INSERT/upsert,
then aborts the second with a marker. Seeing the second-write marker proves the
first statement completed; its transactional counter returns to zero afterward.
Failed explicit index returns exit 1 and one JSON error, retains all old A/B
search tuples/IDs, records failed rather than running and commits no file changes.

With the same trigger, refresh fails A but commits healthy B's one update. A stays
on its old snapshot; B's changed size/time are visible and IDs remain stable.
Root-specific JSON reports the failed root without an index summary and the healthy
root with its committed counters. This is per-root atomicity, not batch rollback.

After removing fault objects, A index succeeds: added 1, updated 2, Missing 1.
Surviving IDs remain stable; the new path gets a new ID and the removed path is
excluded from present search results.

### Incomplete return path

Only A's fixture policy depth becomes 1, then refresh observes one top-level file
and reports four blocked child directories. A has complete=false, errors=4,
observed files=1, added/updated/Missing=0 and missing_reconciled=false. Prior A
tuples/IDs remain intact; B completes with zero changes. The observed file was not
silently committed. Restoring depth 128 and refreshing adds that one file, keeps
survivor IDs, leaves one Missing record and completes both roots.

Final assertions verify no fixture trigger/state-table objects, schema version 3,
original saved depth and no running scan records. No real disk damage, disk-full,
permission failure, cancellation, abrupt kill or concurrent race is simulated.

### Identity

- CLI SHA-256: 7f0c0862407465531761869a98fa41c2cf454ed62ed65e7d13c392c6e23407aa
- Recovery runner SHA-256: 9a7878f62abc3bd665d14de57f28163a14beb1e3036a9d480a0a338a0396a6f8
- Invocation helper SHA-256: c36fd99042e495a80562f21c377f732d3b27ff0ee9749340b90f29e9594ccf09
- Filter helper SHA-256: fd8e6b28dc2dc0ab62c003acababa88b785c867ce0e5ead4f37e6db0abd6eda9
- Mutation helper SHA-256: 7761d6022b2f4f3d3373490b64d25c1bc33254a2f4fb095dc808778130104ab7

## Safety and remaining work

No user-supplied database/source root exists in this tool's arguments. All fault
DDL/DML and file changes occur inside its owned TemporaryDirectory; resolve/check
paths before connecting, refuse absent/outside databases, bind parameters and use
static DDL. Close test connections before CLI calls. File mutation guards verify
metadata/containment/collisions. The product CLI remains read-only toward sources.
This is not a production schema change, organizer or source-file recovery feature.

Reports contain generic labels/counters/hashes, no temporary paths or real user
data. New reports are exclusive and failures prevent a success report; temporary
cleanup is automatic. No privacy collection, credential access or product network
behavior changes. Source edits are not undone by index rollback. Existing evidence
retains its original hashes and is not reinterpreted.

Next smallest scope: deterministic cooperative cancellation retention in application
tests, separate from abrupt process termination. Supported sanitizer/CI evidence
and broader OS failure/concurrency cases remain outstanding.
