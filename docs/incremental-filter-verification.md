# Incremental filter/identity verification — 2026-11-09

Scope: authorized Phase 1 continuation, correctness of existing index/refresh and
filter snapshots across controlled synthetic changes. No product behavior/schema/
domain API change. ADR-0021 records the lifecycle, oracle and mutation boundary.

## Changes and acceptance

- benchmarks/search/verify_incremental.py: bounded two-root generated fixture,
  five phases, 124 filter cases per phase, eight checked scan summaries, five full
  identity snapshots and five catalog status checks. Uses public CLI JSON only.
- verify_filters.py: extract existing tuple/file-ID validation into private shared
  check_search/check_cases helpers; original 120-case online/offline matrix retained.
- tests/e2e/incremental_filter_contract.py and CMake: minimum 3 files per root,
  explicit target-query transitions, scan/status/identity report checks, output
  no-overwrite and rejected 2/1025 bounds. Mutation containment tests use only two
  owned temporary workspaces, with valid metadata so rejection cannot be explained
  by another preflight failure. They prove refusal of outside writes/removals/
  rename destinations and preservation of an occupied rename destination.
- README/status/ADR updated. No C++ source, database or production CLI changes.

## Exact commands and observed results

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure -R 'incremental-filter-contract|filter-matrix-contract'
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset debug --output-on-failure
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Focused tests passed 2/2. Final full Debug and Release suites each passed 9/9,
including strengthened mutation-guard tests. Configure/build/format/lint passed.
Static analysis covers configured C++ sources; Python lifecycle logic is verified
by real E2E execution, not claimed checked by clang-tidy. Policy/spec supplied
trailing spaces remain unchanged and are excluded from scoped whitespace checks.

Supported Linux/Clang CI and ASan/UBSan execution remain pending; combined sanitizer
configuration excludes this Windows environment. No sanitizer, cross-platform
runtime, clean-checkout release acceptance or commit is claimed.

## Recorded generated runs

```powershell
python benchmarks/search/verify_incremental.py --cli build/debug/fileweaver.exe --build-info build/debug/benchmark-build-info-Debug.json --per-root 3 --output build/validation/incremental-debug-min-20261109.json
python benchmarks/search/verify_incremental.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --per-root 24 --output build/validation/incremental-release-20261109.json
```

Both passed 620 filter searches and eight scan checks plus all identity/status
assertions. Default Release report: 275149 bytes, created 2026-11-08 16:28:37 UTC
(2026-11-09 local), two roots/48 initial files, kind
incremental_correctness_only_not_performance. Raw reports remain ignored in build.
No timing or performance comparison is made.

| Phase | Present records | Missing records | Total records | Scan history |
| --- | --- | --- | --- | --- |
| initial | 48 | 0 | 48 | 2 |
| stale_before_reindex | 48 | 0 | 48 | 2 |
| after_index | 48 | 2 | 50 | 4 |
| unchanged_refresh | 48 | 2 | 50 | 6 |
| restored_refresh | 48 | 2 | 50 | 8 |

Root A explicit reindex: added 2, updated 1, Missing 2. Root B: added 0, updated 1,
Missing 0 (same-size mtime change). Unchanged refresh: zero changes in both roots.
After original paths return, root A refresh: added 0, updated 2, Missing 2; root B
zero changes. Every scan complete, no skips/errors, reconciliation true.

Before reindex, all search tuples and IDs equal the old indexed snapshot despite
source edits. Surviving/modified paths retain IDs. Renamed/new paths receive new
IDs. Unchanged refresh preserves the mapping. When old paths reappear, their
original IDs return; two temporary paths become Missing instead. No physical
identity across a rename, file-content recovery or universal undo is claimed.

Release identity:

- CLI SHA-256: 7f0c0862407465531761869a98fa41c2cf454ed62ed65e7d13c392c6e23407aa
- Incremental runner SHA-256: 7761d6022b2f4f3d3373490b64d25c1bc33254a2f4fb095dc808778130104ab7
- Filter helper SHA-256: fd8e6b28dc2dc0ab62c003acababa88b785c867ce0e5ead4f37e6db0abd6eda9
- Shared invocation helper SHA-256: c36fd99042e495a80562f21c377f732d3b27ff0ee9749340b90f29e9594ccf09

## Safety, limits and next scope

Only the runner's owned TemporaryDirectory is mutated. Resolve/check endpoints
inside it, validate expected source metadata, refuse collisions, then operate on
synthetic files. No real source is accepted. These guards are test-fixture helpers,
not a Phase 3 organizer/rename feature, adversarial TOCTOU guarantee or undo system.
The product CLI remains read-only toward sources. No database table is read/written
directly by the verifier; it uses init/index/search/roots/status/refresh use cases.

Reports keep generic cases/counters/hashes without temporary paths or real user
data. No privacy collection, dependency, network behavior or schema migration.
Existing reports are not overwritten or reinterpreted; failures prevent success
reports and fixture cleanup remains automatic. Every child has a deadline.

This controlled small fixture does not establish concurrent disappearance/race,
partial/incomplete/cancelled scan recovery, content-hash correctness or performance.
Next smallest scope: end-to-end retention of the prior index on a failed scan,
without weakening current reconciliation/rollback boundaries.
