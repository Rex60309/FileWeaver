# Phase 1 acceptance ledger

Date: 2026-11-09. Scope: PRODUCT_SPEC section 24 Phase 1 and applicable engineering
gates; not Phase 2 GUI/watchers/fuzzy ranking or later file-changing capabilities.
This ledger distinguishes implemented behavior, executed evidence and open gates.
It is not a release announcement.

**Result: Phase 1 completed and accepted.** Verified implementation/test/preset
revision: `c9cd8fead9bdd9551fe4e5ef146579db0139dbf8`.
[Successful hosted run 42564387945](https://github.com/Rex60309/FileWeaver/actions/runs/42564387945).
Documentation-only completion updates do not change that validated code/config.

| SPEC section 24 acceptance point | Result / evidence |
| --- | --- |
| Index 100000 test files | Passed; independently generated nested Unicode fixture and index counts |
| Correct search results | Passed; independent CLI/native/SQLite/filesystem oracles, filter/lifecycle matrices and error contracts |
| Rescan updates and Missing reconciliation | Passed; creation/update/rename/deletion/reappearance, rollback/recovery and conservative exclusions |
| Reproducible benchmark | Passed; committed driver/fixture generation, commands, environment, raw observations and hashes in linked reports |

| Final hosted gate | Observed result |
| --- | --- |
| Windows 2022, MSVC 19.44.35229.0 Debug | 11/11 groups passed |
| Windows 2022, MSVC 19.44.35229.0 Release | 11/11 groups passed |
| Ubuntu 24.04, GCC 13.3.0 Debug | 11/11 groups passed |
| Ubuntu 24.04, GCC 13.3.0 Release | 11/11 groups passed |
| Ubuntu Clang 18.1.3 ASan/UBSan, strict preset environment | 11/11 groups passed |
| clang-format 23.1.5 / clang-tidy 23.1.4 | Both gates passed |

The hosted jobs use fresh Git checkouts and the pinned vcpkg baseline. Actual job
logs were read and their successful configure/build/test/analysis steps checked,
not merely inferred from workflow configuration. Local Windows storage totals are
63 cases / 802 assertions; local Linux storage totals are 61 cases / 770 assertions.
Platform-specific branches explain the difference; no test quarantine was added.

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
- After initial commit e2a68e0, an independent Git clone with --no-hardlinks
  produced an actual clean checkout in a new ignored directory. Release preset
  configured with the existing pinned vcpkg toolchain and cached dependencies;
  fresh build and all 11 CTest groups passed (42.99 seconds). No pre-existing
  project objects were used. This is not a clean-machine dependency installation
  or packaged-release claim.
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

## Acceptance history (initial failures retained)

Hosted run [42561449394](https://github.com/Rex60309/FileWeaver/actions/runs/42561449394)
on e2a68e0 passed Ubuntu Debug/Release but failed the analysis formatter-version
mismatch and MSVC file-clock compilation. These failures are retained, not labeled
passes. [ADR-0026](decisions/ADR-0026-ci-tooling-and-clock-portability.md) records
the focused fixes. After them, Windows Debug/Release again pass 11 groups each,
format/lint pass, and applicable rebuilt Linux ASan/UBSan storage plus independent
UTC/filter matrix passed 2/2 groups. At that stage a hosted retry was required.

Second run [42562349853](https://github.com/Rex60309/FileWeaver/actions/runs/42562349853)
on b00e72b passed hosted Ubuntu Debug/Release and Clang format/lint/ASan/UBSan.
MSVC compiled but failed canonical-test-workspace assumptions and malformed-path
diagnostic handling. ADR-0026 records the follow-up. No product alias-matching
contract was changed. After those fixes, local Windows Debug/Release, format/lint
and rebuilt Linux ASan/UBSan storage/Unicode/JSON tests pass (3/3 selected groups).

Third run [42563423917](https://github.com/Rex60309/FileWeaver/actions/runs/42563423917)
on 4f1a1ba passed hosted Linux/Clang jobs and Windows JSON/Unicode/creation-time
coverage. Only a unit-test cross-drive relative-path assumption remained; it is
corrected with scoped test CWD and a restoration assertion. Product code is
unchanged by that final test-harness correction. All previous failures remain
recorded rather than silently rerun as presumed transient errors.
After the scoped-CWD correction, local Windows Debug/Release each pass 11 groups,
storage 63 cases / 802 assertions; format/lint pass. Rebuilt Linux sanitizer
storage passes 61 cases / 770 assertions. Application binary SHA-256 still equals
the 4f1a1ba benchmark's f6d20cab... value; this change affects tests only.
The sanitizer test preset now carries the same explicit leak-detection and
halt-on-error environment as the executed local gate, so hosted Clang tests do
not rely on runtime defaults or recover after a UBSan report. No checks disabled.

- Hosted CI has now completed successfully on the verified revision above.
  Release tests run as well as builds; MSVC explicitly compiles sources as UTF-8.
- Linux final Debug/Release and ASan/UBSan pass; actual Git clean-checkout
  Release build/test also passes, as recorded above.
- SPEC 3.2 creation-time gap was explicitly approved and implemented for Windows:
  [creation-time evidence](creation-time-verification.md). Focused 2-case /
  31-assertion and Windows full Debug/Release suites pass. Linux Debug/Release
  and final ASan/UBSan each pass 11 groups, storage 61 cases / 769 assertions.
- Controlled cold-cache, specialized substring-index comparison, 500k/1m scaling
  and broader resource measurements remain unmeasured. Existing reports do not
  claim them. Phase 1's explicit four acceptance points have local evidence;
  whole-product performance/architecture aspirations are not all Phase 1 features.

100k-file benchmark observations: [2026-11-09 measurements](performance/phase1-100k-20261109.md).
All 168 timing samples and six CLI memory observations are correct. Raw fixture,
report and binaries remain ignored. No controlled cold-cache claim is made.

## Completion boundary

No selected Phase 1 implementation or mandatory quality gate remains open.
This does not accept all phases of the product or declare a packaged release.
Phase 2 Qt/hotkey/fuzzy/watcher/preview work has not started. Controlled cold-cache,
specialized substring-index comparisons, larger-scale datasets and broad hardware
performance remain future measurements, not silently claimed Phase 1 results.

The implemented application code is unchanged after 4f1a1ba; later commits fix
only test isolation and sanitizer test environment. The final 100k report's binary
hash was rechecked against the local Release executable after those changes.

User source files remain read-only. No clipboard/content collection, file
management, telemetry or product network behavior was added. GitHub access is
development-only to the user-provided repository; credentials are never committed.
