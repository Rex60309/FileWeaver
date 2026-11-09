# Filter matrix and explicit-empty regression — 2026-11-09

Scope: authorized Phase 1 mixed-size/suffix correctness validation and the CLI bug
it exposed. Restore the existing explicit-empty filter contract; do not change
domain matching, schema, dependencies, source-file permissions or product phase.
ADR-0019 records the generated matrix; ADR-0020 records the parsing fix.

## Failure before the fix

The initial generated run failed online case 60: the explicit empty extension case.
A separate two-file probe showed `--ext` followed by an empty argument returning
both data and data.txt, count 2, instead of only the extensionless data, count 1.
Empty root, modified-after and modified-before similarly returned successful
unfiltered results. Empty numeric bounds already returned exit 2.

After adding filter-matrix-contract, this command failed with the same count-2
regression before changing production code:

```powershell
cmake --preset debug
ctest --preset debug --output-on-failure -R filter-matrix-contract
```

CLI11 optional<string> conversion drops an explicitly empty string. The corrected
CLI stores ordinary text plus option presence, then forwards a supplied value to
existing domain/UTC validation. Empty extension remains engaged; empty root/time
fails exit 2. No option remains no constraint. Existing missing-value/dash-prefixed
literal/JSON behavior is retained. Equals-only empty syntax is not reinterpreted;
README explicitly requires a separate preserved empty argument, not --ext=.

## Files and user-visible changes

- apps/cli/main.cpp: preserve explicit text-filter presence, update extension help.
- benchmarks/search/verify_filters.py: bounded correctness-only generated matrix,
  independent fixture-record oracle, full ordered name/path/size/root-ID comparison,
  positive unique file IDs, online/offline phases, invalid arguments, exclusive report.
- benchmark_search.py: shared invocation accepts explicit cwd/deadline; existing
  benchmark defaults remain unchanged.
- tests/e2e/filter_matrix_contract.py and CMake: failing-before/passing-after empty
  regression, empty root/time/numeric rejection, matrix/report/no-overwrite checks,
  1-file-per-root boundary and invalid 0/1025 bounds.
- README, feature status, ADRs and this evidence updated. No unrelated edits.

## Exact quality commands and results

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure -R filter-matrix-contract
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset debug --output-on-failure
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

After the fix: focused test passed 1/1; complete Debug and Release each passed 8/8
CTest groups. Configure/build/format/lint passed without new suppressions. Python
behavior is covered by actual E2E execution; clang-tidy covers configured C++ only.
Scoped whitespace checks preserve/exclude unchanged supplied policy/spec trailing
spaces. No release test preset exists; Release uses its actual build directory.

Supported Linux/Clang CI and ASan/UBSan remain pending; combined sanitizers exclude
Windows here and were not rerun/claimed passed. No clean-checkout/release acceptance
or commit is claimed. Historical performance reports describe their own binary
hashes, not this corrected binary; no new performance measurement is claimed.

## Default generated runs

```powershell
python benchmarks/search/verify_filters.py --cli build/debug/fileweaver.exe --build-info build/debug/benchmark-build-info-Debug.json --per-root 72 --output build/validation/filter-matrix-debug-fixed-20261109.json
python benchmarks/search/verify_filters.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --per-root 72 --output build/validation/filter-matrix-release-20261109.json
```

Both succeeded: 120 valid cases per phase, 240 full ordered result checks and 14
invalid argument cases. The Release report was created 2026-11-08 16:10:28 UTC
(2026-11-09 local), 105948 bytes, kind correctness_only_not_performance. It records
144 files across two Unicode roots, 152064 logical bytes, four exact sizes, six
suffix forms and four fixed UTC days. All checks correct; 54 valid searches returned
empty results across both phases. Raw reports remain ignored under build.

Release artifact SHA-256:

- CLI: 7f0c0862407465531761869a98fa41c2cf454ed62ed65e7d13c392c6e23407aa
- Matrix runner: 19433aefdcbf20b6e7309fe4a40030ca80a8a061c67306224a9eaa59f65f85c2
- Shared helper: c36fd99042e495a80562f21c377f732d3b27ff0ee9749340b90f29e9594ccf09

## Safety and remaining work

Only generated files/databases in a unique owned TemporaryDirectory are written.
Both source/destination endpoints are checked inside that workspace, destinations
must not exist, and only the two owned roots are renamed for offline validation.
The product CLI never renames files. No real user root/content, credentials,
clipboard/activity collection, network behavior or schema migration is involved.
Reports store generic case/root labels and counts, not temporary or personal paths.
All child calls have deadlines; failure prevents a success report, existing reports
are never overwritten. Fixture cleanup is automatic.

The matrix is representative, not exhaustive Cartesian coverage; fixed small
payloads/mtime patterns and valid Unicode are explicit limits. Empty equals-only
syntax remains a documented parser limitation. Next smallest slice: filter snapshot
consistency after controlled synthetic incremental creations/updates/removals.
