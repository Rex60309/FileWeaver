# Search baseline — 100000 synthetic files

Measured: 2026-11-08 09:27 UTC on one development machine. These results describe
this run; more testing is needed to check the specification's latency targets.

## Reproduction

```powershell
cmake --preset release
cmake --build --preset release --parallel
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --files 100000 --repetitions 5 --warmups 1 --storage-kind ssd --storage-model 'NVMe KINGSTON SKC3000S1024G' --output build/benchmarks/search-100k-20261108.json
```

Use a new output filename for another run; existing reports are never overwritten.
The original raw report is in the ignored local build directory at the path above.
It contains all 126 samples, query cases, hashes and environment metadata; it is
not bundled into the repository. Re-run the command to produce a fresh report.

## Observed environment and dataset

- CPU: 13th Gen Intel Core i7-13700KF, 24 logical CPUs reported.
- RAM: 34104291328 bytes reported by Windows GlobalMemoryStatusEx.
- OS: Windows-11-10.0.26200-SP0 / AMD64.
- Compiler/build: GNU 13.2.0, C++20, Release, CMake 3.29.2.
- Fixture storage: C: maps to disk 0, NVMe KINGSTON SKC3000S1024G; Get-PhysicalDisk
  reported SSD. Model/type were independently read, then supplied as operator labels.
- Python 3.13.9, Python SQLite 3.51.0. Native CLI SQLite version is not instrumented
  in the report; do not assume both SQLite builds are identical.
- Python driver peak RSS: 35479552 bytes. CLI peak RSS: unmeasured.
- Dataset: 100000 physical zero-byte files in one temporary directory, names
  doc_000000_cat0.txt through doc_099999_cat9.txt, unique deterministic names.
- Generation: 34.2871853 s. Initial index process call: 23.082797 s, including
  process launch/JSON decoding. Index reported 100000 added files.
- Queries: exact first/last/absent; substring doc_000 / _cat3 / not-present.
  Limit 100. One first-observed pass, one warmup pass, five measured passes.
- All 126 timed method calls passed exact ordered name/path/size comparisons
  against an independent deterministic filename oracle. First/warmup samples
  remain separate from the measured summaries.

## Warm measured samples

Nearest-rank percentiles pooled over three queries per mode, five repetitions
(15 samples per row). Milliseconds, rounded to four decimal places.

| Method/envelope | Mode | p50 ms | p95 ms |
| --- | --- | ---: | ---: |
| CLI process + native search + JSON + Python decoding | exact | 14.8762 | 19.5988 |
| CLI process + native search + JSON + Python decoding | substring | 44.1130 | 54.0007 |
| Python SQLite forced full scan, new connection/query | exact | 25.7102 | 30.1327 |
| Python SQLite forced full scan, new connection/query | substring | 42.9827 | 48.1540 |
| Python flat directory filename scan + selected stats | exact | 92.8186 | 100.5623 |
| Python flat directory filename scan + selected stats | substring | 104.1882 | 123.9488 |

Each row times different work and may use a different SQLite version, so the
numbers cannot isolate the effect of the C++ algorithm. Five measured repetitions
per query give only a rough picture of slower queries. This run did not calculate
confidence intervals, control background load/power, or repeat on other machines.

## Cache, provenance and remaining coverage

No OS cache eviction. Building/indexing the fixture already warms caches; the
first-observed samples are not cold-cache results. Measured samples follow explicit
warmup, with method order rotated. All fixtures were removed after measurement.

Binary SHA-256: 0e3830abd9095483b460cb8fc89d0e87a2c6a67ff298577eee1d249779794416.
Runner SHA-256: 2584bbfff221b5bdf675afcd5be278829ae3ec49a1889054bed6cc562fa9918f.
Dependency baseline is included in the raw report. No personal/temp paths, host or
user names were found in the report's path-pattern review.

At the time of this run, core-only timing, native peak RSS, cold-cache tests and
comparisons with optimized/trigram/fuzzy search were still missing. Larger, more
varied datasets and other platforms also need testing before checking the SPEC
exact/substring/fuzzy p95 targets or Phase 1 release readiness.
