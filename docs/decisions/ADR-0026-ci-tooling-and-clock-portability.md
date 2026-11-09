# ADR-0026: Reproducible analysis tools and portable file-clock conversion

Date: 2026-11-09
Status: Accepted as fixes required by executed Phase 1 CI

## Context

Hosted CI for e2a68e0 passed Ubuntu Debug/Release but exposed two failures:
Ubuntu's default clang-format 18 disagreed with locally verified clang-format 23
on short declaration blocks; MSVC rejected direct file_clock::to_sys calls.
Neither failure may be bypassed by disabling checks or weakening warnings.

## Decision and alternatives

Pin the already used analysis tools in tools/requirements-analysis.txt:
clang-format 23.1.5 and clang-tidy 23.1.4. CI installs them to its workspace-local
build/tools/python location, already supported by CMake discovery, and prints
versions. This replaces unspecified OS formatter selection; no production
dependency is added. Reformatting sources back and forth between differing
formatter defaults is not a reproducible alternative. Existing format/lint checks
and their failure behavior remain enabled.

Use C++20 clock_cast<system_clock> when the library advertises complete C++20
chrono support. Keep file_clock::to_sys fallback for the existing GCC library
which provides that conversion without clock_cast. Apply the same adapter to the
wide preflight and exact-duration conversion; preserve range validation before
nanosecond narrowing. Do not estimate epochs from now() or sample wall clocks.
This private implementation changes no public API, schema or UTC filter contract.

The [Microsoft file_clock documentation](https://learn.microsoft.com/en-us/cpp/standard-library/file-clock-class?view=msvc-170)
explains that implementations may expose to_utc/from_utc instead of to_sys/from_sys
and recommends clock_cast for portable conversion. Existing independently fixed
UTC timestamp fixtures remain the correctness oracle in CLI filter tests.

## Verification and consequences

The original hosted run records the failing MSVC compile and format gate. Rerun
Windows Debug/Release, format/lint and applicable Linux sanitizer tests locally;
push the focused fixes and require hosted MSVC/GCC/Clang jobs to actually pass.
No sanitizer, format or lint check is skipped. Pinned-tool installation uses
development network access only in explicitly authorized CI/developer setup;
FileWeaver has no runtime network behavior.

## Follow-up from the second hosted run

b00e72b passed hosted Ubuntu Debug/Release and the complete Clang analysis/
sanitizer job. Hosted MSVC compiled but exposed native path assumptions:
conversion can throw system_error with ERROR_NO_UNICODE_TRANSLATION rather than
filesystem_error/EILSEQ. Catch the common system_error base, recognize only the
two encoding-failure codes and keep the existing escaped-native diagnostic.
Unrelated system errors still propagate. Existing malformed-name rollback tests
are retained, not weakened or quarantined.

Root-filter tests had assumed the temporary directory spelling was already the
canonical registered spelling. Resolve only the newly created private test
workspace in C++/Python before generating fixtures. Keep all root membership,
offline search and cancellation assertions. Product root filtering remains
lexical/read-only as ADR-0007 requires; do not introduce live canonicalization or
alias matching into search to satisfy a test fixture assumption.
