# CLI search memory observations — 100000 synthetic files

Measured: 2026-11-08 15:27 UTC on one Windows development machine. This report
records process memory peaks for the queries below. It does not measure the
whole application, diagnose leaks or set a maximum for other workloads.

## Reproduction and report

```powershell
cmake --preset release
cmake --build --preset release --parallel
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --core-runner build/release/search_core_benchmark.exe --cli-memory --build-info build/release/benchmark-build-info-Release.json --files 100000 --repetitions 5 --warmups 1 --storage-kind ssd --storage-model 'NVMe KINGSTON SKC3000S1024G' --output build/benchmarks/search-cli-memory-100k-20261108.json
```

Raw version 3 report remains in ignored build at that path: 49990 bytes, 168 timed
samples all correct, plus six separately verified CLI memory observations. All six
returned available, so environment.cli_peak_rss_bytes is 11444224. Choose a new
output filename on rerun; reports are exclusively created and never overwritten.
No fixture payload/database, executable or raw report is committed.

## Environment and fixture

- Intel Core i7-13700KF, 24 logical CPUs; RAM 34104291328 bytes.
- Windows 11 build 26200, AMD64; Release GNU 13.2.0, C++20, CMake 3.29.2.
- Python 3.13.9 / Python SQLite 3.51.0; native helper SQLite 3.54.1.
- SSD label NVMe KINGSTON SKC3000S1024G, same temporary-fixture drive as preceding
  runs. Mapping/media type was previously verified read-only in this session.
- Dependency baseline ec48c11eae9448c8c6e7739d93e06fa2433aa95f.
- Flat generated directory, 100000 unique zero-byte ASCII .txt files. Limit 100.
  Generation 32.988665 s; indexing process 27.7613091 s.
- Six queries: exact first/last/absent and substring prefix/category/absent.
  One first-observation round, one warmup, five measured rounds per timed method;
  nearest-rank p50/p95 over 15 measured calls per method/mode.

## CLI memory observations

Each row is a new real CLI process, launched after all timing/native measurements,
with its full ordered name/path/size results verified. Retain a separately opened
PROCESS_QUERY_LIMITED_INFORMATION handle through exit, then query the OS counter.
Source GetProcessMemoryInfo.PeakWorkingSetSize, unit bytes, metric peak_working_set,
capture point after_process_exit_with_retained_query_handle. No private Popen
handle or unrelated PID is queried. All error codes were null in this run.

| Mode | Query | Lifetime peak bytes | Status |
| --- | --- | --- | --- |
| exact | doc_000000_cat0.txt | 9830400 | available |
| exact | doc_099999_cat9.txt | 9830400 | available |
| exact | not-present.txt | 9826304 | available |
| substring | doc_000 | 11444224 | available |
| substring | _cat3 | 11444224 | available |
| substring | not-present | 11390976 | available |

Maximum in this observed set: 11444224 bytes, about 10.91 MiB. This is not memory
of the timed processes themselves, an upper bound for all queries, concurrent
memory, current allocation or leak evidence. API retrieval may fail on other
systems; incomplete coverage keeps the complete summary null and labels any
partial maximum separately. This run establishes Windows post-exit retrieval only.

Separate native helper lifetime peaks before/after workload: 7929856 / 11706368
bytes, before its report serialization. Python driver peak: 34635776 bytes. These
are different processes/scopes and should not be subtracted or added into a
claimed overall concurrent product peak.

## Latency in this run

Milliseconds; different timing envelopes are not algorithm speed ratios.

| Method | Mode | p50 ms | p95 ms |
| --- | --- | --- | --- |
| CLI process + JSON/Python decode | exact | 14.6237 | 17.3798 |
| CLI process + JSON/Python decode | substring | 47.7862 | 51.3922 |
| Python SQLite forced full scan | exact | 51.3321 | 61.0902 |
| Python SQLite forced full scan | substring | 82.0184 | 90.5722 |
| Python flat-directory scan | exact | 173.8978 | 190.1694 |
| Python flat-directory scan | substring | 194.7714 | 206.6929 |
| C++ application search | exact | 0.9511 | 1.1299 |
| C++ application search | substring | 35.6737 | 40.6451 |

No cache eviction or CPU/background-load isolation. Fixture/indexing warms OS
caches; native calls run after the other timed methods, CLI memory cases afterward.
The application interval includes connection/transaction/schema/SQL/result creation,
excluding request preparation, result destruction, correctness and serialization.
Cold-cache behavior and SPEC targets were not tested. The production algorithm
was unchanged. Unicode/nested/large datasets and other platforms still need checks.

## Identity and safety

- CLI SHA-256: cde08cdd8644558ddc39e5308a2c94544cb3fcbb1b585b6a98e95d971e139a11
- Native helper SHA-256: 01df0fbdeed7c907418739545657168b0c18a22c91ce362cc40d63efcfa9b391
- Python runner SHA-256: 0ad6a1af91b07a31f7e874fac683786e56faf45ee0b83eb0e4679e6da85be90e
- CLI observer SHA-256: 08e7a46bb8647ee9b37ad0471b493699318ccc308f97560b705dd0c4d669747f

The actual product CLI binary is unchanged from preceding runs. No real user root,
content, personal path, process enumeration, credentials, database schema or product
network behavior is involved. Historical reports keep their original identities.
