# Native benchmark memory baseline — 100000 synthetic files

Measured: 2026-11-08 15:11 UTC. Single-machine Windows developer measurement, not
whole-product memory acceptance or evidence of a performance/memory improvement.

## Reproduction and artifacts

```powershell
cmake --preset release
cmake --build --preset release --parallel
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --core-runner build/release/search_core_benchmark.exe --build-info build/release/benchmark-build-info-Release.json --files 100000 --repetitions 5 --warmups 1 --storage-kind ssd --storage-model 'NVMe KINGSTON SKC3000S1024G' --output build/benchmarks/search-memory-100k-20261108.json
```

Raw report stays in ignored build at that path: version 2, 48140 bytes, 168 samples
all correct, including 42 native calls. Choose a new output filename to rerun.
No generated payload/database/report or binary is committed. No real user root or
content is accessed. Reports omit fixture paths and user/computer names.

## Environment and data

- CPU: Intel Core i7-13700KF, 24 logical CPUs; RAM 34104291328 bytes.
- Windows 11 build 26200, AMD64; Release GNU 13.2.0, C++20, CMake 3.29.2.
- Python 3.13.9, Python SQLite 3.51.0; native helper SQLite 3.54.1.
- SSD label: NVMe KINGSTON SKC3000S1024G, same temporary-fixture drive as the
  preceding baseline. Mapping/media type were verified read-only in that run.
- Dependency baseline: ec48c11eae9448c8c6e7739d93e06fa2433aa95f.
- Flat generated directory, 100000 unique zero-byte ASCII .txt files; limit 100.
  Generation 33.8488021 s, indexing process 27.9024287 s.
- Exact cases: first, last and absent names; substring cases: prefix, category
  and absent. One first-observed round, one warmup and five measured rounds.

## Memory observations

| Field | Bytes | Status |
| --- | --- | --- |
| Native before-workload lifetime peak | 7921664 | available |
| Native after-workload lifetime peak | 11718656 | available |
| Python driver's separate process peak | 35241984 | available |
| CLI process peak | null | unmeasured |

Native source: GetProcessMemoryInfo.PeakWorkingSetSize; metric peak_working_set;
unit bytes. The observed native after-workload peak is about 11.18 MiB.
Both values are process-lifetime high-water marks, not current usage. They include
startup, arguments, oracle, results/validation and sample buffers through the
second snapshot, before report serialization/teardown. Do not subtract these
peaks to estimate search allocations or diagnose leaks. They do not account for
unresident allocations or the system-wide filesystem cache. Snapshot calls are
outside timed search intervals. See ADR-0016 for API references and failure states.

## Timing observations in this run

Milliseconds, nearest rank pooled across 15 measured calls per method/mode.
Different envelopes are not algorithm speed ratios; caches are uncontrolled and
already warm. Native method runs after other baselines. No CPU/background-load
isolation. No change in the search algorithm was made.

| Method | Mode | p50 ms | p95 ms |
| --- | --- | --- | --- |
| CLI process + JSON/Python decode | exact | 14.4686 | 16.7897 |
| CLI process + JSON/Python decode | substring | 49.1970 | 53.7253 |
| Python SQLite forced full scan | exact | 51.4428 | 58.1195 |
| Python SQLite forced full scan | substring | 81.7830 | 90.6876 |
| Python flat-directory scan | exact | 159.2008 | 175.5577 |
| Python flat-directory scan | substring | 178.3971 | 195.7659 |
| C++ application search | exact | 0.9091 | 1.3257 |
| C++ application search | substring | 35.2484 | 38.6636 |

Native interval includes connection open/close, read transaction, schema checks,
SQL preparation/execution and result construction; excludes request construction,
result destruction, oracle/validation and JSON. No reused connection or controlled
cold-cache claim. Small capped results and synthetic ASCII fixtures cannot establish
memory/latency targets for larger varied real datasets, GUI or the whole product.
Linux runtime/sanitizer evidence, CLI peak RSS and optimized-index comparisons remain.

## Identity

- CLI SHA-256: cde08cdd8644558ddc39e5308a2c94544cb3fcbb1b585b6a98e95d971e139a11
- Native helper SHA-256: 01df0fbdeed7c907418739545657168b0c18a22c91ce362cc40d63efcfa9b391
- Python runner SHA-256: 2cc285ba75c6494dd73d0b7103025f7eb435155d16254119e801c33da637e2be

Historical timing-only reports retain null native-memory fields and their original
hashes; this run does not retrospectively fill or reinterpret those measurements.
