# Core application benchmark verification — 2026-11-08

Scope: authorized continuation of Phase 1, one developer measurement slice.
Add in-process timing of the existing search_files use case; do not change search
semantics, domain API, schema, source-file permissions or product phase.
Reuse the existing generated fixture, CLI initialization/indexing and Python
hardware/report runner. ADR-0015 records timing boundaries and alternatives.

## Implementation and acceptance

- benchmarks/search/core_search.cpp: read-only native benchmark helper, bounded
  argument ranges, deterministic six-query oracle and per-call steady_clock times.
- benchmarks/search/benchmark_search.py: optional --core-runner, native SQLite
  version/binary hash, report version 2 and additional raw/summary samples.
- CMakeLists.txt: build, strict warnings, format/lint/supported-sanitizer settings
  for the helper; pass helper path to the existing benchmark CTest.
- tests/e2e/benchmark_contract.py: original three-method smoke, native sample/report
  coverage, one-file boundary with Unicode fixture root, wrong-size oracle failure
  with empty stdout and invalid argument rejection. No timing threshold assertions.
- README, feature status and ADR updated. Original no-core report path retained.

Every native call compares ordered name/path/size tuples outside timing. Python
also checks sample counts against its separate oracle. A failed helper yields no
successful final report. Existing reports are still exclusively created.
Source fixtures/databases are generated in owned temporary directories. No real
user root/content or personal paths are stored in reports. No network requests.

## Quality commands

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Debug and Release build and CTest passed 5/5 groups each. Format passed.
Initial lint detected easily swapped path/integer arguments; replaced those
arguments with an internal Configuration value, without suppressions. Final lint
passed. [Release 100k-file measurement](performance/core-search-baseline-100k.md):
168 samples correct, including 42 native calls; native measured p95 1.1127 ms exact
and 35.0718 ms substring. Raw report contains environment, hashes and all samples.
These are warm OS-cache application intervals with the limits stated below.

No release test preset exists, so Release CTest uses its actual build directory.
The original supplied policy/spec contain pre-existing trailing spaces and are
excluded from the implementation whitespace check. Unrelated work is preserved.
ASan/UBSan execution remains unavailable on this Windows configuration; supported
Linux/Clang CI execution is still pending. No sanitizer or clean-checkout/release
acceptance is claimed and no commit is made while that evidence is pending.

## Limits and remaining work

Measure the complete application call, including connection open/close, read
transaction, schema checks, SQL preparation and result construction. No reused
connection; SQLite per-connection caches are recreated. OS caches are uncontrolled
and already warm from fixture generation/indexing/other baselines. Exclude request
construction, result destruction, correctness checks and JSON. No claim about
raw SQL-only time, controlled cold-cache behavior, peak RSS or SPEC p95 compliance.
Next smallest measurement milestone: native-process memory accounting; controlled
cold-cache and optimized-index comparisons remain independently scoped work.
