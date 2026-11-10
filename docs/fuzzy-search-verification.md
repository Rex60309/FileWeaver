# Bounded fuzzy filename search verification

Date: 2026-11-10 (Asia/Taipei). Phase 2 scope explicitly approved: common C++20
matcher/search use case, CLI fuzzy mode, Windows Qt mode/settings, configurable
ranking, no schema or content indexing changes. See ADR-0031 for the algorithm,
resource limits, public API and alternatives. No release packaging/new Linux work.

## User-visible changes

CLI --mode fuzzy defaults to relevance order; literal modes retain their defaults.
Windows mode chooser adds fuzzy filename matching and a session-only settings
dialog for all five weights/candidate budget. Cancel discards settings. Both use
search_files and the existing bounded/cancellable search worker. No score fields
are added to JSON. Typo edits count Unicode scalars, folding ASCII case only.
All tokens must match; short tokens have limited/no typo tolerance. Filename-only
matching does not implement the spec's later path-component search. No persistence.

Candidates use subsequence/length/multiset scalar and bigram bounds before bounded
Levenshtein. Candidate overflow is a visible failure, not silent recall loss.
The filtered SQLite metadata scan/sort can still be costly. No content reads,
file mutations, database migration, dependency, clipboard collection, activity
collection or application network call was added. Search works on offline roots.
Existing confirmed open/reveal behavior remains separate and unchanged.

## Test evidence and corrections

Core cases cover Unicode/ASCII folding, typo insertion/deletion/substitution,
transposition costing two edits, multi-token/subsequence rules, q-gram recall for
generated one-edit neighbors, malformed/oversized input, weight validation,
pre-cancellation, stable ties, filters, v2 reads, sort conflicts and candidate
budget errors. CLI E2E creates isolated multilingual fixtures, verifies settings,
filters, result limits, JSON errors, offline results and unchanged database bytes.
Qt interaction tests cover selected fuzzy mode/relevance requests, settings
accept/cancel and reuse of the background worker. Existing search/hotkey/open/
reveal tests remain enabled. CTest fake handoffs never launch files or Explorer.

Initial 100k baseline failed because the zero bigram bound admitted unrelated
short Unicode/ASCII words. Added a necessary multiset scalar-overlap bound and a
regression; the same baseline then passed. Two easily-swappable-parameter lint
errors were fixed with typed internal policy/request values, without suppression.
Focused core tests passed 146 assertions in two cases. Full Windows core
Debug/Release CTest passed 12/12 (56.90/52.98 seconds); desktop Debug/Release passed
13/13 (58.71/56.05 seconds). Qt reports 18 passes, 0 failures/skips/blacklisted
tests in both configurations. Minimal Qt emits expected plugin size-hint warnings.
Both Debug format-check and lint targets passed for core and desktop. All four
configure/build presets passed. git diff --check passed.

## Reproducible baseline

[Raw measurements](performance/fuzzy-100k-windows-20261110.json) contain all 24
observations: first uncontrolled run and five warm runs per query, with individual
result correctness and Windows child peak-working-set readings. Intel i7-13700KF,
24 logical CPUs, 34,104,291,328 bytes RAM, Windows 11 build 26300, GCC 13.2,
Release/C++20/CMake 3.29.2. Storage medium was not established (unknown SSD/HDD).
Generated SQLite metadata: 99,997 unrelated ASCII filenames and three target
filenames, including Unicode, typo and multi-token cases. No source crawl.

| Query | Warm p50 ms | Warm p95 ms |
| --- | --- | --- |
| needle | 92.96 | 97.94 |
| nedle | 99.17 | 103.90 |
| img proc fin | 87.25 | 106.69 |
| 日本😀 | 87.21 | 93.06 |

This measures CLI startup, SQLite scanning/candidate/ranking, JSON and observation
overhead on an interactive desktop, not core-only latency. All 24 memory readings
were available; maximum child peak working set was 11,808,768 bytes. No OS cache
eviction: the first run is not controlled cold-cache evidence. This is the first
fuzzy baseline, not a speedup comparison or proof of the spec's general latency
or memory targets. Dense matching catalogs can exceed the candidate budget.

```powershell
python benchmarks/search/fuzzy_search.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --output build/fuzzy-baseline.json --rows 100000 --repetitions 5
```

## Quality gates

Local Windows GCC 13.2 / Qt 6.11.3. Commands executed for the selected platform:

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel
ctest --preset desktop-debug --output-on-failure
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel
ctest --preset desktop-release --output-on-failure
git diff --check
```

Tests set TEMP/TMP to build/test-temp and create only generated fixtures. Windows
combined ASan/UBSan is unsupported by these presets; no local sanitizer or new
manual Linux result is claimed. No native Windows screenshot is claimed for this
slice; the Qt interaction suite uses the minimal platform with process-local
clipboard. Hosted acceptance has not yet been recorded for this revision.

## Remaining Phase 2 work

Path matching, watcher/reconciliation and bounded safe previews remain planned.
The next proposed smallest milestone is a user-selected Windows watcher with
explicit overflow/restart reconciliation; it requires separate scope approval.
