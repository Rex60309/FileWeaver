# Native memory benchmark verification — 2026-11-08

Scope: authorized continuation of Phase 1, native benchmark memory accounting.
Acceptance: report own-process before/after memory high-water marks in bytes with
metric/source/scope/status, explicit unavailable values, preserved search oracle
and compatibility with the old helper. No production schema, domain API, search
semantics, source-file mutation or privacy-sensitive collection changes.

## Changes and evidence

- benchmarks/search/process_memory.hpp: private OS adapter for Windows process
  PeakWorkingSetSize and Linux RUSAGE_SELF maximum RSS. Linux KiB conversion has an
  overflow check. OS errors, invalid values and unsupported systems return null
  plus status. Only the benchmark's current process is queried.
- core_search.cpp: read memory before workload and after search/validation, outside
  timed calls and before JSON construction. Emit source, metric, unit and scope.
- benchmark_search.py: keep native metadata, fill existing core_peak_rss_bytes,
  preserve absent-helper nulls and old-helper missing-memory status. Version 2 is
  unchanged; this is additive developer-report metadata.
- CMake: Windows system psapi link, header in format-check. No external package.
- Benchmark E2E: require integer positive nondecreasing peaks, matching byte field,
  expected API/metric and scope on supported systems; explicit null on unsupported
  systems and no-core mode. Preserve all oracle, argument and exclusive-report tests.
- README, feature status and ADR-0016 document exact scope and remaining limits.

Before implementation, the new benchmark check failed with KeyError core_memory.
After implementation the focused test and both complete suites pass.

Before rebuilding Release, the updated Python driver was also run against the
actual preceding Release helper (SHA-256
59bfd11377786903dbe29bc90ce6da782f33843407f0ed066c598caced886bb3):

```powershell
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --core-runner build/release/search_core_benchmark.exe --build-info build/release/benchmark-build-info-Release.json --files 30 --repetitions 2 --warmups 1 --smoke --output build/benchmarks/memory-legacy-helper-check.json
```

Observed success, core_peak_rss_bytes null and core_memory.after_status
helper_does_not_report_memory. This report is test evidence, not performance data.

## Quality gates

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure -R benchmark-contract
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Debug/Release builds passed; each complete CTest suite passed 5/5 groups. Format
and lint passed, without new suppressions. The release test preset does not exist;
use the actual Release build directory as above. Diff check preserves original
supplied policy/spec trailing spaces and excludes those unchanged input files.

Windows OS-query positive branch executed. Linux code/unsupported-platform branch
and forced OS-query failure are not claimed executed locally. Linux/Clang CI and
ASan/UBSan remain pending; the combined sanitizer configuration excludes Windows.
No sanitizer, clean-checkout release acceptance or commit is claimed.

## Observed Release measurement

[100k-file memory baseline](performance/native-memory-baseline-100k.md) records the
exact command, hardware/build/storage, raw report path, hashes and all summaries.
168 samples passed correctness. Windows native lifetime peak before workload:
7921664 bytes; after workload before JSON: 11718656 bytes (about 11.18 MiB).
Both statuses available, source GetProcessMemoryInfo.PeakWorkingSetSize. No search
speedup or memory reduction is claimed. Original historical reports are retained.

## Measurement boundaries and remaining work

The API reports a process-lifetime high-water mark through the second snapshot,
not one-query allocated bytes. It includes startup/arguments, oracle, result
validation and sample storage, but excludes subsequent report serialization and
teardown. Do not subtract peaks to diagnose allocations or leaks. Linux counters
can include pre-exec resource history. Working set/RSS also excludes unresident
memory and system-wide filesystem caches. API documentation is linked in ADR-0016.

Only generated temporary zero-byte source files/databases and a new local report
are written. No real user roots or contents are accessed. Product networking and
database compatibility are unchanged. Next smallest measurement slice: CLI process
memory accounting. Controlled cold-cache, optimized-index comparisons and supported
Linux execution remain independently scoped work.
