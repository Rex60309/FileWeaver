# Phase 1 acceptance ledger

Date: 2026-11-09. Scope: PRODUCT_SPEC section 24 Phase 1 and applicable engineering
gates; not Phase 2 GUI/watchers/fuzzy ranking or later file-changing capabilities.
This ledger distinguishes implemented behavior, executed evidence and open gates.
It is not a release announcement.

| Phase 1 deliverable | Implementation / evidence |
| --- | --- |
| File scanner | Explicit roots, streaming regular-file metadata, exclusions, bounded depth, Unicode, no default link traversal |
| SQLite index | Schema v3, transactional migration/rollback, root policies, serialized writer, read-only inspection/search |
| Incremental indexing | Create/update, rename as new path plus Missing, unchanged-record retention, reappearance IDs, failed/incomplete/cancelled retention |
| Exact / substring search | Bound queries, deterministic sorting/limits, ASCII folding for substring |
| Filters | Extension, inclusive logical size, exact-root membership, strict UTC modification bounds, sorting |
| JSON | One stdout document, stderr diagnostics, documented stable exit codes |
| Benchmark | Generated 100000-file fixtures, independent correctness oracle, raw timing and memory reports |
| Additional selected slice | Literal filename prefix, lifecycle/v2/cancellation regression evidence |

## Executed local gates

- Windows GCC 13.2.0 Debug/Release: 11 CTest groups each; storage 61 cases / 770
  assertions before creation-time work; final creation-time revision passes
  63 cases / 801 assertions in Debug and Release. Format and lint passed.
  Latest detailed regression evidence:
  [prefix verification](prefix-search-verification.md).
- Independent source export: 94 tracked source/document/config files, exclusive
  new destination under ignored build, per-file SHA-256 manifest. Fresh Windows
  vcpkg Debug and Release builds and all 11 groups passed. Uses cached dependencies,
  not pre-existing project objects. No committed revision existed at export time;
  this is a clean-source build, not a Git clean-checkout claim.
- Initial direct-prefix export linked successfully but could not locate SQLite
  DLL at runtime. Reconfigured independent empty build directories using the
  existing vcpkg toolchain, which deploys the runtime DLL. Nothing was silently
  skipped or worked around in the product tests.
- Ubuntu WSL2 GCC 13.3.0: Debug 11/11 passed. Combined ASan/UBSan 11/11 passed
  with ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 and
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. Dependencies are separately
  compiled Release libraries; sanitizer instrumentation covers project targets,
  not all third-party code. Windows-only native-console/junction branches are
  verified by Windows runs, not claimed as Linux coverage.
- Linux dependencies were built offline from the cached CLI11 2.7.2 / Catch2
  3.16.1 sources and SQLite 3.54.1 archive. No packages installed or network used
  for local builds. Build logs show subsecond WSL/NTFS make timestamp-skew
  warnings; completed binaries were then actually tested.

## Reproduction

Standard Windows gates:

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Linux local commands (inside the existing Ubuntu distro, from repository root):

```sh
cmake --preset sanitizers -G 'Unix Makefiles' -B build/linux/sanitizers \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_PREFIX_PATH="$PWD/build/linux/dependencies" -DCMAKE_TOOLCHAIN_FILE:FILEPATH=
cmake --build build/linux/sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/linux/sanitizers --output-on-failure
```

Debug/Release use the corresponding repository preset, binary directory and
CTest directory. Unix Makefiles is a generator override because this distro has
make rather than Ninja. This does not change repository preset defaults.

## Open acceptance items

- Hosted CI has not yet completed. A Release test preset and Release CI test step
  were added; MSVC explicitly compiles project sources as UTF-8.
- Linux Release initially passed 11/11 groups; final-revision reruns and a Git
  clean-checkout run are pending recording.
- SPEC 3.2 creation-time gap was explicitly approved and implemented for Windows:
  [creation-time evidence](creation-time-verification.md). Focused 2-case /
  31-assertion and Windows full Debug/Release suites pass. Linux Debug/Release
  and final ASan/UBSan each pass 11 groups, storage 61 cases / 769 assertions.
- Controlled cold-cache, specialized substring-index comparison, 500k/1m scaling
  and broader resource measurements remain unmeasured. Existing reports do not
  claim them. Phase 1's explicit four acceptance points have local evidence;
  whole-product performance/architecture aspirations are not all Phase 1 features.

User source files remain read-only. No clipboard/content collection, file
management, telemetry or product network behavior was added. GitHub access is
development-only to the user-provided repository; credentials are never committed.
