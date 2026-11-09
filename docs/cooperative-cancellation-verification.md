# Cooperative cancellation checkpoints — 2026-11-09

Scope: authorized Phase 1 deterministic validation of existing scanner/index/
refresh cancellation and recovery. No production source/interface/schema change.
ADR-0023 records callback/clock checkpoints and their limits.

## Changes

tests/integration/storage.cpp adds three tagged C++ cases and small private helpers
reusing the existing owned Fixture. Snapshot tuples contain file ID, root ID,
filename, display path and logical size. Sources are tiny generated files; roots
include Unicode names. No sleeps, timer races, worker threads, process kills,
SQLite global hooks or production test flags are introduced.

| Case | Controlled stop point | Observed contract |
| --- | --- | --- |
| Scanner | metadata delivery count 2 or 4 | Exact prefix delivered, cancelled/incomplete, no extra callbacks |
| Scanner | reused already-stopped token | Zero callbacks/files |
| Scanner recovery | fresh default token | All 6 files remain readable as metadata |
| Index | before invocation or injected start clock | Zero delivery/committed changes, old tuples/IDs retained, cancelled root/history |
| Index recovery | new invocation | added 1, updated 1, Missing 1; surviving ID retained/new path distinct |
| Refresh | before invocation | No roots attempted, no new scans, old snapshot retained |
| Refresh | first root start clock | First root cancelled, second not attempted; both old snapshots retained |
| Refresh | first root finish clock | Completed first root commits its update; second not attempted; batch cancelled |
| Refresh recovery | fresh default token | Only outstanding updates applied, IDs retained, completed states/no running scans |

Root policies remain known with depth 128 and empty exclusions. Index cancellation
records cancelled rather than running; a refresh that attempts no roots does not
invent scan history. Finish-clock cancellation arrives after the first root's
scan work has completed; it does not promise to undo that completed root or provide
batch-wide atomicity. The returned attempted/index/exit fields distinguish an
unattempted root from the catalog's previous completed state.

## Exact verification commands

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
.\build\debug\storage_tests.exe '[cooperative-cancellation]' --reporter compact
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset debug --output-on-failure
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Final observed results:

- Focused tag: 3 cases / 317 assertions passed.
- Full Debug and Release CTest: 10/10 groups each passed.
- Both storage logs: 57 C++ cases / 700 assertions passed (previously 54/383).
- Configure/build/format and final lint passed. Initial lint caught unchecked
  optional.value access in two new assertions and an oversized private enum;
  replaced with safe optional comparisons and explicit uint8_t. No suppression.
  Rebuilt/re-ran focused tests and both complete suites after those corrections.
- Full assertion totals were read from each build's
  Testing/Temporary/LastTest.log, not inferred from planned additions.

The supplied original policy/spec contain unchanged trailing spaces and remain
excluded from scoped implementation whitespace checks. No tests are quarantined
or silently skipped. Supported Linux/Clang ASan/UBSan and CI execution remain
pending; combined sanitizer configuration excludes this Windows environment.
No sanitizer, cross-platform execution, clean-checkout release acceptance or commit
is claimed. No performance/latency/memory measurements were added for this slice.

## Safety and remaining evidence

All source writes/removals occur within the test's own generated temporary Fixture;
the product remains read-only toward user files. No real roots/content, privacy
collection, database migration, dependency or network behavior changes. Existing
metadata/data files are not used as demonstrations. Historical reports retain
their hashes and are not reinterpreted.

These tests do not force cancellation while an indexing transaction has only some
rows staged during enumeration. Partial staged-write rollback is independently
covered by failed/incomplete scan tests, without claiming that as executed mid-row
cooperative cancellation. SQL progress cancellation, OS signals/Ctrl+C, abrupt
termination and hard cancellation latency are not established. CLI graceful Ctrl+C
remains unimplemented. Broader cancellation and supported sanitizer evidence remain.

Next proposed product slice, pending selection: literal filename prefix mode,
ASCII-case-insensitive like substring, reusing existing filters/sorting/limits and
read-only storage. It is planned, not implemented by this cancellation test task.
