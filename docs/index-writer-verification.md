# Index writer and CLI verification

Date: 2026-11-08
Scope: Phase 1 explicit-root metadata persistence and conservative re-scanning.
Environment: Windows GCC 13.2.0; existing pinned dependencies; schema remains v2.
Design and limitations: ADR-0005-index-writer.md.

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed with existing local toolchain/cache |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 2/2 including CLI index E2E |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 28 cases / 145 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed, no project warnings/errors |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 2/2 |
| `build/debug/fileweaver.exe index --help` | Passed; root/database/exclusion options displayed |

CTest used authorized permissions to create synthetic junction fixtures. No tests
were skipped. Source files/databases were generated in isolated temporary fixtures.
The E2E fixture indexes a file containing spaces and an apostrophe in its filename.
The integration suite also covers Unicode roots and deterministic injected clocks.

## New acceptance evidence

- Initial metadata persistence, unchanged indexed_at retention, changed metadata,
  renamed/new paths and missing old paths, and removed-file reconciliation.
- Incomplete scans roll back observed file updates and preserve prior records.
- Pre-requested cancellation retains records and persists cancelled status.
- Trigger-induced write failure rolls back metadata and persists failed status;
  a subsequent scan succeeds after removing the synthetic trigger.
- Database and SQLite sidecar exclusions; database hardlink aliases are excluded.
- Missing database returns CLI exit 4 without creating it.
- User values are bound to SQL; quoted filenames persist correctly.

Two failing regressions were observed and then fixed:

1. `build/debug/storage_tests.exe 'Embedded nulls*' --reporter compact` initially
   failed because an embedded-NUL scan root was accepted. After validation was
   added, both root and exclusion assertions passed.
2. `build/debug/storage_tests.exe 'Absent excluded*' --reporter compact` initially
   failed (missing=1, expected 0). Reconciliation now requires no configured user
   exclusions as well as no actual skips. The regression passes.

## Safety and remaining work

No schema or dependency changes, network downloads, source-file mutation, content
reads or real-user scans were performed in this slice. Database writes include
full paths and metadata; SQLite files should be protected accordingly. Index
cancellation is available through the C++ API, not a graceful CLI Ctrl+C handler.
Status records can remain running after abrupt process termination; no automatic
history repair or multi-process scan ownership is implemented. The writer holds
a serialized transaction during traversal and has not been benchmarked.

Missing reconciliation is intentionally deferred for any configured user exclusion
or any skipped entry, including an in-root database/reparse point. Stale records
may remain. Store the database outside roots when full reconciliation is desired.
Exact path keys can produce distinct roots for case variants; universal path/file
identity deduplication is not claimed. Existing scanner path/race limitations apply.
Range checks exist for size/count/timestamp conversion; no huge-file/timestamp
stress fixture, permission ACL or multi-process concurrency test was executed.

Supported-platform ASan/UBSan and CI remain pending. No release/performance claims
or commits were made. Next smallest slice: bound exact/substring search against
present records with deterministic ordering, filters and CLI/output tests.
