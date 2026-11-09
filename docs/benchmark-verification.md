# Developer benchmark verification

Date: 2026-11-08
Scope: Phase 1 synthetic search baseline runner, build provenance and correctness
smoke test. Decision: ADR-0013-search-benchmark.md. No production code/schema changes.

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed; generated Debug build-info |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure -R benchmark-contract` | Passed, 1/1 |
| `ctest --preset debug --output-on-failure` | Passed, 4/4 |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed |
| `cmake --preset release` | Passed; generated Release build-info |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 4/4 |

Real Release command, actual environment/percentiles and original raw-report path
are recorded in [100k baseline](performance/search-baseline-100k.md). That run
generated/indexed 100000 physical synthetic files and verified all 126 timed
method calls. No build/tests were run concurrently with the measurement.
The raw JSON includes compiler/build info, hashes, CPU/RAM/OS, storage labels,
query cases, first/warmup/measured samples and nearest-rank percentiles.

The smoke test uses 30 generated files and two repetitions, checks report shape,
sample counts/correctness, no-overwrite behavior, invalid file-count rejection
and the Debug performance-run guard. Smoke timings are explicitly not performance
evidence and never become CI latency thresholds. Test assertion optimization is
rejected. Existing test cases are retained without weakening checks.

## Safety and limitations

Python stdlib only; no dependency/schema changes, network requests or real-user
root scans. Dataset files/databases are created in unique owned temporary folders
and removed. Reports use exclusive creation and omit temporary/user/computer names.
The real report's path-pattern review found no personal/temp paths. Raw reports
remain under ignored build, not committed application logs/artifacts.

These are CLI-process and Python baseline envelopes, not directly comparable
native-core measurements. First-observed is not cold-cache: no OS cache eviction
was attempted. Dataset is flat, ASCII names, zero-byte files; five repetitions
per query offer limited tail evidence. Driver RSS is measured where available;
native CLI peak RSS, controlled cold-cache, optimized/fuzzy index, cross-machine/
platform replication and larger/richer datasets remain unmeasured. CPU power and
background workloads were not controlled. No target compliance/speedup/release
claim is made. CI/supported-platform ASan/UBSan remain pending.
