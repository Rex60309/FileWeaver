# Application search baseline — 100000 synthetic files

Measured: 2026-11-08 14:54 UTC on one development machine. Caches were already
warm; this run alone cannot establish the specification's latency targets.

## Reproduction

```powershell
cmake --preset release
cmake --build --preset release --parallel
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --core-runner build/release/search_core_benchmark.exe --build-info build/release/benchmark-build-info-Release.json --files 100000 --repetitions 5 --warmups 1 --storage-kind ssd --storage-model 'NVMe KINGSTON SKC3000S1024G' --output build/benchmarks/search-core-100k-20261108.json
```

Use a new output filename on rerun. Original raw report remains in ignored build
at the path above: version 2, 47642 bytes, 168 samples, all correct. No report,
generated file/database or executable is committed. Report contains no fixture
paths, user/computer names or real file metadata.

## Environment and fixture

- CPU: Intel Core i7-13700KF, 24 logical processors.
- RAM: 34104291328 bytes; Windows 11 build 26200, AMD64.
- Release, GNU 13.2.0, C++20, CMake 3.29.2.
- Python 3.13.9 / SQLite 3.51.0. Native helper linked SQLite 3.54.1; CLI version
  remains uninstrumented and is not inferred from the helper.
- Temporary storage on disk 0, NVMe KINGSTON SKC3000S1024G, SSD. Read-only
  Get-Partition/Get-Disk and Get-PhysicalDisk confirmed mapping/media type.
- Dependency baseline: ec48c11eae9448c8c6e7739d93e06fa2433aa95f.
- 100000 unique zero-byte ASCII .txt names in one flat generated directory.
  Generation 35.9009351 s; indexing process 21.30783 s. Limit 100 per query.
- Exact: first name, last name, absent name. Substring: prefix, category, absent.
  One first-observation round, one warmup round, five measured rounds per method.
  Nearest-rank p50/p95 pooled over 15 measured calls per method/mode.

## Observed results

Times are in milliseconds, rounded to four decimal places. Check what each
method times before comparing rows; they include different amounts of work.

| Method | Mode | p50 ms | p95 ms |
| --- | --- | --- | --- |
| CLI process + JSON/Python decode | exact | 11.8909 | 14.6078 |
| CLI process + JSON/Python decode | substring | 43.1438 | 45.7211 |
| Python SQLite forced full scan | exact | 45.2251 | 51.0940 |
| Python SQLite forced full scan | substring | 66.6793 | 98.3665 |
| Python flat-directory scan | exact | 156.8189 | 180.8998 |
| Python flat-directory scan | substring | 181.9326 | 218.9550 |
| C++ application search call | exact | 0.7336 | 1.1127 |
| C++ application search call | substring | 31.8119 | 35.0718 |

The application interval includes connection open/close, read transaction, schema
validation, statement preparation, SQL and result vector construction. It excludes
process launch, request/oracle construction, validation, destruction of returned
results and JSON. Connections are not reused. Each native sample checks ordered
name/path/size tuples outside its interval, and Python checks expected counts.

The native method runs after all three baselines; its first observation is already
influenced by warm OS caches. Fixture generation/indexing also warms caches. No
cache eviction, cold-cache claim, CPU isolation or background-load control.
The next useful checks are different queries, larger multilingual/nested datasets
and repeated independent runs. The search algorithm was unchanged for this run.

Driver peak RSS: 34983936 bytes. CLI/native-helper peak RSS are unmeasured.
This run did not test an optimized-index baseline, controlled cold caches or
compliance with SPEC targets.

## Artifact identity

- CLI SHA-256: cde08cdd8644558ddc39e5308a2c94544cb3fcbb1b585b6a98e95d971e139a11
- Native helper SHA-256: 59bfd11377786903dbe29bc90ce6da782f33843407f0ed066c598caced886bb3
- Python runner SHA-256: 9f135e571079bda6b371a8c5c0377c238fef30e0d227f2a23928b36690591189

Historical ADR-0013 reports describe their own recorded binaries; differences
between these independent runs are not attributed to performance improvements.
