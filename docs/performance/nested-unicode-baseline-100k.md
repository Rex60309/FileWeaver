# Nested Unicode search baseline — 100000 synthetic files

Measured: 2026-11-08 15:48 UTC on one development machine. The results apply to
the dataset below; a different dataset would need its own comparison.

## Reproduction and identity

```powershell
cmake --preset release
cmake --build --preset release --parallel
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --core-runner build/release/search_core_benchmark.exe --cli-memory --dataset nested-unicode --build-info build/release/benchmark-build-info-Release.json --files 100000 --repetitions 5 --warmups 1 --storage-kind ssd --storage-model 'NVMe KINGSTON SKC3000S1024G' --output build/benchmarks/search-nested-unicode-100k-20261108.json
```

Raw report remains in ignored build at the path above: version 4, 51486 bytes,
168 timed samples all correct plus six independently verified CLI memory cases.
All six memory readings were available. Reports are exclusively created; use a
new filename on rerun. No raw payload/database/report or executable is committed.

- CLI SHA-256: cde08cdd8644558ddc39e5308a2c94544cb3fcbb1b585b6a98e95d971e139a11
- Native helper SHA-256: e8bdf2b0317ce09d84f7b7ad0cbb8d415c1945579180a695b51fe224e99f6586
- Python runner SHA-256: 65d66dd67f93669c3cdc3a5657393ee5adc289e772cd25f333e09a0bfbc17d39
- CLI observer SHA-256: 08e7a46bb8647ee9b37ad0471b493699318ccc308f97560b705dd0c4d669747f

The actual product CLI is unchanged. Historical profile reports keep their own
hashes/data; this run does not alter or reinterpret them.

## Environment and shape

- Intel Core i7-13700KF, 24 logical CPUs; RAM 34104291328 bytes.
- Windows 11 build 26200, AMD64; Release GNU 13.2.0, C++20, CMake 3.29.2.
- Python 3.13.9 / SQLite 3.51.0; native helper SQLite 3.54.1.
- SSD label NVMe KINGSTON SKC3000S1024G, same temporary-fixture drive verified
  read-only in preceding runs of this session.
- Dependency baseline ec48c11eae9448c8c6e7739d93e06fa2433aa95f.
- 100000 generated zero-byte .txt files, 33334 distinct multilingual names, up to
  three copies per basename across three branches/eight second-level buckets.
  Exactly 27 child directories, two levels; Unicode/spaces/emoji in root and DB.
- Generation 36.9620826 s; indexing process 28.4828576 s; result limit 100.
- One first-observation round, one warmup, five measured rounds. Nearest-rank
  p50/p95 pooled over 15 measured calls per method/mode.

## Correctness

Python tuple oracle, native independent fixture oracle, actual CLI, forced SQLite
full scan and recursive filesystem baseline agree on complete name/path/size/order.

| Mode | Query | Returned count |
| --- | --- | --- |
| exact | 檔案_日本語_é😀_000000_類0.txt | 3 |
| exact | 檔案_日本語_é😀_033333_類3.txt | 1 |
| exact | 不存在.txt | 0 |
| substring | 日本語 | 100 |
| substring | _類3 | 100 |
| substring | 不存在 | 0 |

The 100-result limit cuts through a three-copy name group. Oracles compare the
selected full paths, not merely counts/names. Matching folds ASCII only; all
fixture strings are valid Unicode. This does not add Unicode normalization or
support malformed native UTF-16 names. No user files/content are used.

## Observed latency

Times are in milliseconds. Each method includes different work, so ratios between
rows do not isolate algorithm speed. The recursive baseline uses os.walk and
reads metadata for selected files.

| Method | Mode | p50 ms | p95 ms |
| --- | --- | --- | --- |
| CLI process + JSON/Python decode | exact | 14.0057 | 15.1567 |
| CLI process + JSON/Python decode | substring | 62.6297 | 67.5942 |
| Python SQLite forced full scan | exact | 71.3176 | 78.3612 |
| Python SQLite forced full scan | substring | 116.2147 | 134.0435 |
| Python recursive directory scan | exact | 235.9071 | 268.3243 |
| Python recursive directory scan | substring | 444.1479 | 1924.5398 |
| C++ application search | exact | 0.9303 | 1.5555 |
| C++ application search | substring | 49.4916 | 53.6737 |

Recursive substring measured tails include 1924.5398 ms and 1890.1130 ms for
日本語; both were correct. Raw samples are retained, not filtered. Cause is not
established. No CPU/background-load isolation or cache eviction; fixture/indexing
warms caches. Native calls run after other timed methods, memory searches afterward.
Application timing includes connection/transaction/schema/SQL/result construction,
excluding request/oracle/validation, result destruction and JSON. The run did not
measure raw SQL alone, cold-cache behavior or progress toward SPEC latency targets.
The search algorithm was unchanged.

## Observed memory

Bytes, platform-specific process-lifetime high-water marks with distinct scopes.

| Process/snapshot | Peak bytes |
| --- | --- |
| Native helper before workload | 8015872 |
| Native helper after workload, before JSON | 11808768 |
| Maximum of six extra CLI cases | 11534336 |
| Python driver | 34586624 |

Windows source GetProcessMemoryInfo.PeakWorkingSetSize. CLI maximum is 11.00 MiB
for this case set, observed through retained handles after each process exited;
it is not memory of the timed processes, per-query allocation, a universal cap,
concurrent product peak or leak evidence. Do not subtract/add these process peaks.
Unresident memory and system-wide filesystem cache are outside this metric.
Cross-platform/sanitizer evidence and broader size/type/filter workloads remain.
