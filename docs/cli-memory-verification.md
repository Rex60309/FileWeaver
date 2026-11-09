# CLI child memory observation verification — 2026-11-08

Scope: authorized Phase 1 continuation, opt-in developer measurement of actual CLI
search children. No production CLI/domain/schema/source-file behavior change.
Acceptance: six separately verified search observations after timing, OS-reported
byte peaks with capture point/source/status, complete-versus-partial distinction,
safe child/handle cleanup and reproducible report identity. ADR-0017 records the
retained-handle approach, alternatives and explicit evidence limits.

## Changes

- benchmarks/search/cli_memory.py: private Windows query adapter, own explicit
  child's retained limited-query handle, post-exit reading, null/error metadata,
  timeout/exception kill/reap, handle closure, validated partial/complete summaries.
- benchmark_search.py: --cli-memory adds a separate six-case oracle-checked pass,
  version 3 metadata, observer SHA-256 and optional environment.cli_peak_rss_bytes.
  Fill that summary only when every case's memory value is available. Version 1/2
  and no-observation mode remain available. Timing samples do not include this pass.
- tests/e2e/cli_memory_contract.py: actual 16 MiB touched allocation, self-read
  high-water mark, release before exit, retained-handle result at least as large;
  nonzero exit, draining stdout/stderr larger than pipes, timeout propagation with
  child reaped and both pipes closed. Synthetic metadata tests cover partial,
  missing/unsupported readings and reject empty/zero/false availability.
- Benchmark E2E checks version/hash/case counts, unchanged latency sample count,
  result correctness and full-versus-partial summary behavior. Unsupported-platform
  expectations are configured for CI, not claimed executed locally.
- CMake registers the new Python E2E. .gitignore excludes Python bytecode caches.
  README/feature status updated; ADR-0016 points to the follow-up decision.

## Exact quality commands and results

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure -R 'cli-memory-contract|benchmark-contract'
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset debug --output-on-failure
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Focused tests passed 2/2. Debug and Release full CTest each passed 6/6; configure,
build, format and lint passed. C++ sources are unchanged for this slice. Scope of
format/lint remains the repository's configured C++ gates; Python logic is exercised
by actual E2E tests, not claimed checked by clang-tidy. Original supplied policy/
spec trailing spaces remain preserved and are excluded from scoped diff checks.

Windows child/self high-water comparison and timeout cleanup executed successfully.
Partial/open/query-failure statuses were checked with synthetic metadata, not
forced failures of the actual OS API. No post-exit-query guarantee for every OS is
claimed. API/open/query failure retains null and explicit status/error codes.
Linux/unsupported runtime execution and Linux/Clang ASan/UBSan CI remain pending.
The combined sanitizer preset excludes Windows; no sanitizer or release/commit
acceptance is claimed and no commits were made.

## Actual Release observation

[100k-file CLI memory run](performance/cli-memory-baseline-100k.md) records the exact
command, hardware/build/storage, complete raw sample locations and artifact hashes.
168 latency samples plus six independent CLI memory cases all passed correctness.
All six readings were available. Largest observed CLI working set: 11444224 bytes
(about 10.91 MiB). This set's maximum is not a product-wide upper bound or a memory
reduction claim. Raw report version 3 includes all per-case statuses/error codes.

## Safety and remaining work

Only benchmark-created direct children are queried; no process enumeration or
unrelated-process monitoring. Popen receives an explicit argument list with no
shell, pipes are drained, errors/timeouts propagate, and owned handles close.
The CLI is the unchanged Release executable. Fixture files/databases are generated
in owned temporaries; only a new report is written. No real user roots/content,
schema migration, credentials, telemetry or product networking changes.

CLI observations are six extra processes, not memory readings from timed samples.
Their maximum describes that observed case set, not all searches, combined peaks,
per-query allocations or leak evidence. Working set/RSS is platform-specific and
does not include unresident allocations/system-wide cache. Existing timing/memory
reports are not rewritten. Next smallest benchmark milestone: generated Unicode
and nested-directory dataset coverage. Controlled cold cache, optimized-index
comparison and cross-platform runtime evidence remain separate work.
