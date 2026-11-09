# Phase 1 scanner slice verification

Date: 2026-11-08
Environment: Windows, GCC 13.2.0, Ninja, existing pinned foundation dependencies.
Scope: standalone streaming metadata scanner library; no CLI/index/schema change.

## Acceptance and results

Explicit-root streaming, Unicode/spaces, logical sizes, modification timestamps,
empty/invalid roots, exclusions, cancellation before/during traversal, callback
failure, disappearing files, junction skipping and linked-ancestor rejection,
bounded depth and invalid depth settings, Windows namespace rejection are tested.
No test intentionally scans real user roots. Fixtures are generated temporary
directories. The suite has 18 Catch2 cases including foundation storage coverage,
with 82 assertions; all passed on Debug. CTest has two entries (suite and CLI).

| Exact command | Result |
| --- | --- |
| `cmake --preset debug` | Passed using existing compiler/vcpkg cache configuration |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 2/2 |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 18 cases / 82 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed; no project warnings/errors |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 2/2 |

## Failures resolved and limitations

MinGW's standard-library symlink fixture creation returned "Function not
implemented". Replaced that fixture with a native Windows junction; no link
test was skipped in the final suite. The restricted execution environment denied
FSCTL_SET_REPARSE_POINT with error 5. Running the same suite with the authorized
test permissions passed. Standard CI hosts must allow junction creation.

Initial compiler macro redefinition and clang-tidy findings were corrected.
Clang-tidy uses its own intrinsic headers instead of GCC private intrinsic
headers; enabled checks were not disabled. Dependency/system warnings remain
excluded from project warnings by normal clang-tidy system-header handling.

An initial extended-path 130-directory fixture stalled and was stopped. Windows
network/device/extended namespace roots are now explicitly rejected (including
forward-slash spelling), with regression assertions. The depth limit is bounded
at 128 and configurable downwards; a five-directory fixture with limit 3 verifies
pruning/error/incomplete behavior without unsupported long paths. No Windows
long-path support is claimed. No permission-denied ACL fixture or hostile
directory-replacement containment test was executed; those remain limitations.

CI and supported-platform sanitizer execution remain pending, as in Phase 0.
This Windows host does not support the configured combined ASan/UBSan preset.
There are no benchmark claims or release claims, and no commits were made.

## Safety and next slice

The scanner reads filesystem metadata only; it never opens source file contents.
No production database migration, dependency download, network behavior or source
file mutation was introduced. Only generated test files were created/deleted.
Read-only status checks are not race-proof against malicious path replacement.
The indexing use case must explicitly exclude its database/cache paths and must
never use an incomplete/cancelled scan as deletion evidence. Next: transactional
index persistence with upgrade/rollback tests, then CLI indexing and search.
