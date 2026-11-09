# Nested Unicode benchmark verification — 2026-11-08

Scope: authorized Phase 1 developer benchmark continuation. Add a generated
nested-unicode profile, duplicate basename/path tie coverage and report identity;
preserve production search semantics, schema, dependencies and source-file safety.
Acceptance: independent Python/native ordered tuple oracles, recursive baseline,
combined memory options, boundary/invalid-profile tests and existing quality gates.
ADR-0018 records the profile formula, ordering and report-version decisions.

## Changes

- benchmark_search.py: --dataset flat-ascii|nested-unicode; multilingual source/DB
  paths and repeated names across three branches/eight second-level buckets;
  at most 27 directories, zero-byte generated files; profile/count metadata and
  version 4 for the new profile. Defaults preserve flat fixture/cases/method names.
- Python oracle selects the smallest complete (name, path, size) tuples, not names
  alone. Substring matching folds UTF-8 ASCII bytes only, matching SQLite's current
  lower semantics; no implicit Unicode normalization/case-folding expansion.
- Nested filesystem method is separately labeled python_recursive_directory_scan;
  os.walk raises enumeration errors and does not follow generated directory links.
- core_search.cpp: independently generates nested fixture entries/cases, validates
  complete ordered paths and self-identifies the dataset. Uses explicit Unicode
  escapes in UTF-8 C++ literals to avoid source-code-page dependence. Timing/memory
  boundaries are unchanged. New profile is sent only when explicitly selected.
- CMake/new nested-benchmark-contract: 103-file test proves exact counts 3/1,
  substring count 100 with a cut through a three-name group, category count 12 and
  absent count 0, for all four methods. Includes native+CLI memory, 1-file boundary,
  invalid profiles in both programs, UTF-8 JSON and no fixture paths in reports.
- README/status/ADR updated; all prior flat/report/oracle failure tests retained.

## Commands and results

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure -R 'nested-benchmark-contract|benchmark-contract'
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset debug --output-on-failure
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Focused tests passed 2/2; complete Debug/Release suites passed 7/7 groups each.
Configure/build/format/lint passed without suppressions. Static analysis covers
configured C++ sources; new Python behavior is verified by real E2E tests, not
claimed analyzed by clang-tidy. Source policy/spec pre-existing trailing spaces
are preserved and excluded from scoped implementation whitespace checks.

Linux/Clang CI and ASan/UBSan execution remain pending; combined sanitizers exclude
this Windows configuration. No sanitizer, cross-platform runtime, clean-checkout
release acceptance or commit is claimed. Unknown profiles fail before report
creation; old flat tests continue to verify exclusive/no-overwrite output behavior.

## Actual Release run

[100k-file nested Unicode baseline](performance/nested-unicode-baseline-100k.md)
records the exact command, complete environment, method envelopes, raw report
location/hashes and unfiltered timing tails. 100000 files, 27 directories and
33334 distinct names: 168 timed samples and six CLI memory cases all correct.
Native warm p95: exact 1.5555 ms, substring 53.6737 ms. Largest observed CLI working
set 11534336 bytes. No performance improvement or SPEC target acceptance claimed.

## Safety and limits

Only owned generated temporary source files/directories/databases and a new local
report are written. No real user root or contents, malformed native filenames,
clipboard/activity records or network telemetry are accessed. No schema/domain API
or production CLI change. Benchmark child observation remains opt-in and separate
from timing. Existing history/hashes are retained, not retroactively reinterpreted.

This fixture uses valid Unicode, exactly two child-directory levels and zero-byte
.txt files. It does not fix unpaired native UTF-16 units or add Unicode normalization.
No controlled cold cache, CPU/load isolation, algorithm speedup or SPEC target
claim. Query/name/path distribution differs from flat ASCII, so do not infer a
speed ratio between profiles. Next smallest dataset slice: varied logical sizes
and file extensions with composable filter correctness, independently scoped.
