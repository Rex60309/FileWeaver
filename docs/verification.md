# Foundation verification — 2026-11-08

Phase 1 acceptance and current cross-platform evidence: [Acceptance ledger](phase1-acceptance.md).

Latest filename mode: [Prefix search verification](prefix-search-verification.md).

Latest cooperative checkpoints: [Cancellation verification](cooperative-cancellation-verification.md).

Latest failed/incomplete retention: [Failure recovery verification](failure-recovery-verification.md).

Latest incremental/filter lifecycle: [Incremental verification](incremental-filter-verification.md).

Latest filter parsing fix/mixed fixture: [Filter matrix verification](filter-matrix-verification.md).

Latest multilingual nested profile: [Nested benchmark verification](nested-benchmark-verification.md).

Latest CLI child observations: [CLI memory verification](cli-memory-verification.md).

Latest native memory accounting: [Memory benchmark verification](memory-benchmark-verification.md).

Latest native application timing: [Core benchmark verification](core-benchmark-verification.md).

Latest Unicode filename fix: [Unicode verification](unicode-verification.md).

Latest synthetic benchmark: [Benchmark verification](benchmark-verification.md)
and [100k measured baseline](performance/search-baseline-100k.md).

Latest roots/status and test totals: [Inspection verification](inspection-verification.md).

Latest schema v3/refresh and test totals: [Refresh verification](refresh-verification.md).

Latest JSON output and test totals: [JSON verification](json-verification.md).

Latest sorting and test totals: [Sort verification](sort-verification.md).

Latest UTC modification-time filters and test totals: [Time filter verification](time-filter-verification.md).

Latest search filters and test totals: [Filter verification](filter-verification.md).

Latest filename search and test totals: [Search verification](search-verification.md).

Later scanner changes and current test totals: [Phase 1 scanner verification](scanner-verification.md).
Latest schema and test totals: [Index schema v2 verification](index-schema-verification.md).
Latest CLI indexing and test totals: [Index writer verification](index-writer-verification.md).

Scope: Phase 0 CLI help, SQLite initialization and build infrastructure.
Not a completed foundation: CI and supported sanitizer execution are pending.

Executed on Windows with GCC 13.2.0 (Strawberry distribution):

| Command | Result |
| --- | --- |
| `cmake --preset debug -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe` | Passed |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 2/2 CTest entries, including 7 Catch2 storage cases |
| `cmake --build --preset debug --target format-check` | Passed, clang-format 23.1.5 |
| `cmake --build --preset debug --target lint` | Passed, clang-tidy 23.1.4; compiler implicit include paths supplied for MinGW |
| `cmake --preset release -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 2/2 |

Earlier automatic selection of C:/MinGW failed due to a system DLL relocation
error. A working GCC was subsequently found without installing a compiler.
Initial format/lint failures were resolved by installing the tools and explicitly
passing the compiler's implicit include paths to clang-tidy. No warning checks
were disabled. Combined ASan/UBSan was not run on this Windows host. CI has not run.
No commits were made because CI/supported sanitizer acceptance remains pending.

Dependencies installed after explicit user authorization: vcpkg baseline
ec48c11eae9448c8c6e7739d93e06fa2433aa95f, CLI11 2.7.2, SQLite 3.54.1,
Catch2 3.16.1. Development downloads occurred; runtime network behavior was not
introduced. Tests write synthetic temporary databases only. Schema v1 is recorded
in ADR-0002. No user source files were scanned or modified.

## Reproducing the local setup

After cloning/bootstrap of the pinned vcpkg into build/tools/vcpkg, run in
PowerShell (all generated dependencies remain under ignored build directories):

```powershell
$env:PATH = 'C:\Strawberry\c\bin;' + $env:PATH
$env:VCPKG_ROOT = (Resolve-Path build/tools/vcpkg).Path
./build/tools/vcpkg/vcpkg.exe install --triplet x64-mingw-dynamic --host-triplet x64-mingw-dynamic --x-install-root=build/dependencies
$installed = Join-Path (Get-Location) 'build/dependencies'
cmake --preset debug '-DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe' '-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic' '-DVCPKG_HOST_TRIPLET=x64-mingw-dynamic' "-DVCPKG_INSTALLED_DIR=$installed" '-DVCPKG_MANIFEST_INSTALL=OFF'
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release '-DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe' '-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic' '-DVCPKG_HOST_TRIPLET=x64-mingw-dynamic' "-DVCPKG_INSTALLED_DIR=$installed" '-DVCPKG_MANIFEST_INSTALL=OFF'
cmake --build --preset release --parallel
ctest --test-dir build/release --output-on-failure
```

Manifest install was disabled only after successful explicit installation of the
same manifest. An initial attempt to rerun vcpkg in the restricted process failed
with CreateFileW stdin access denied while extracting tools. This does not disable
dependency discovery or quality gates. Pip tools are installed in build/tools/python;
CMake detects their native executables there. Windows WSL inventory failed with
E_ACCESSDENIED; no supported Linux runtime was available through that check.

Remaining: execute CI/sanitizers on a supported host, then implement Phase 1
metadata scanning and search with a separate schema migration and acceptance tests.

## Final diff review

`git diff --check` after intent-to-add reports only the existing Markdown hard
line breaks (trailing double spaces) in the supplied AGENTS.md and PRODUCT_SPEC.md.
Those source documents were preserved. `git diff --check -- . ':!AGENTS.md'
':!PRODUCT_SPEC.md'` passes for implementation files. Direct Catch2 execution
(`build/debug/storage_tests.exe --reporter compact`) passed 26 assertions across
7 cases. Initial intent-to-add required an elevated retry because .git/index.lock
creation was denied; it then succeeded. No commits or remote pushes were made.
