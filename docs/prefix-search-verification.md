# Filename prefix search verification — 2026-11-09

## Scope and implementation

Phase 1 selected slice: `search QUERY --mode prefix`. The default remains substring.
Append the domain enum value, map CLI input, and select a constant SQL predicate
`instr(lower(name),lower(?1))=1`. Query and filter values remain bound parameters.
No schema, dependency, network or source-file mutation changes. Decision:
[ADR-0024](decisions/ADR-0024-filename-prefix-search.md).

Acceptance: literal filename-start matching, ASCII-only case folding, Unicode
preservation, existing filter/order/limit composition, unchanged JSON and default
behavior. No glob, fuzzy, path search or prefix performance claim.

## Regression and coverage

Before implementation, after registering the new test:

```powershell
cmake --preset debug
ctest --preset debug --output-on-failure -R '^prefix-contract$'
```

Observed rejection: `Invalid arguments: --mode: prefix not in {exact,substring}`.
After implementation, the test initially reached its help assertion and failed:
it inspected top-level help instead of search help. Corrected that assertion to
`search --help`; no production validation was weakened.

`tests/e2e/prefix_contract.py` creates 54 synthetic files under two temporary
Unicode roots. An independent Python oracle checks 134 combinations twice, with
roots online then renamed offline: **268 ordered filter cases**. Includes size,
extension, root, strict UTC dates, sort direction, limits and deterministic ties.
Additional count probes cover CJK, emoji, ASCII folding, distinct é/É, internal
tokens, filename versus path, literal quotes/percent/underscore/star, default
substring, invalid mode/empty query, UTF-8 human output and subcommand help.
Complete matching result snapshots and catalog status remain unchanged after
offline searches. Renames/writes are restricted to owned generated fixtures.

## Commands and observed results

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

Debug and Release: **11/11 CTest groups passed**, including prefix-contract.
Both storage logs retain **57 cases / 700 assertions passed**; no additional C++
test cases are claimed. Configure/build, format-check and lint passed. Lint uses
the existing repository configuration; no new suppression was added. The scoped
whitespace check passed. The original policy/spec trailing spaces are unchanged.

Windows GCC 13.2.0 was used. The configured combined ASan/UBSan preset excludes
Windows; Linux/Clang sanitizer and CI execution remain pending. No cross-platform,
clean-checkout release acceptance or commit is claimed.

## Limits and next evidence

Prefix folds ASCII only, without Unicode normalization. It searches stored present
metadata without live revalidation. The function predicate may scan indexed rows;
historical benchmarks do not establish prefix speed or memory use. There are no
new measurements or index-acceleration claims.

The follow-up below adds dedicated prefix-specific regression evidence for
schema v2, Missing exclusion and pre-cancellation. Broader features require
milestone selection. User files and private content were not accessed; no
telemetry, schema migration or network behavior was introduced.

## Follow-up: lifecycle, legacy reads and cancellation — 2026-11-09

Scope: complete the previously proposed prefix regression milestone without
changing production contracts, schemas or dependencies.

Four C++ cases tagged `[prefix-search]` exercise literal anchoring and ASCII-only
folding, Unicode and SQL-like characters, real temporary-source Missing
reconciliation and reappearance preserving file IDs, v2 reads with composable
filters/strict time bounds/order/limits, and pre-cancellation followed by retry.
Invalid empty/NUL/4097-byte queries and limit bounds are rejected; 4096-byte query
and limits 1/10000 are accepted. Cancellation is checked as StorageError code 1
with the cancellation message, rather than merely accepting any exception.

The generated CLI oracle matrix now runs 134 combinations in five states:
online, offline, reconciled Missing, restored and legacy-v2, for **670 ordered
filter checks**. Reappearance preserves the complete original result including
IDs. The legacy fixture is a private SQLite backup of the test database with v3
policy tables removed and user_version set to 2, following existing v2 fixtures.
Both C++ and CLI cases verify version 2 and absence of policy tables afterward;
search does not migrate it or append scan records.

Focused command:

```powershell
.\build\debug\storage_tests.exe "[prefix-search]"
```

Observed: 4 cases / 70 assertions passed. Full quality commands are the same as
listed above. Follow-up results: Debug and Release each passed 11/11 CTest groups;
their storage logs each report 61 cases / 770 assertions and prefix-contract
reports 670 ordered checks. Debug total time was 37.17 seconds, Release 33.90
seconds (suite durations, not search latency benchmarks). Debug/Release configure
and build, format-check, lint and scoped git diff --check all passed.

Two test implementation errors were observed and corrected: strict compilation
rejected signed/unsigned comparison in the eight-file fixture assertion; and
Python sqlite3 connection context managers did not close handles, preventing
Windows temporary cleanup. The assertion now compares the fixed expected count;
connections use contextlib.closing. No production checks or test assertions were
removed to obtain passing results.

This verifies pre-cancellation only. It does not establish cancellation during
SQLite execution, cancellation latency, Ctrl+C, power-loss recovery or sanitizer
coverage. No timing or memory claims were added. Linux/Clang sanitizer/CI and
clean-checkout release acceptance remain outstanding.
