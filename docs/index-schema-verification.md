# Index schema v2 verification

Date: 2026-11-08
Scope: Phase 1 durable index schema only; no index writer or CLI index command.
Design: ADR-0004-index-schema.md. No new dependencies or runtime network behavior.

Windows GCC 13.2 with existing pinned vcpkg configuration:

| Command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 2/2 |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 21 cases / 102 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 2/2 |

Tests use generated temporary databases and directories only. The full suite
requires permission to create synthetic Windows junction fixtures, as before.
Fresh creation, repeated initialization, v1 upgrade preserving settings,
mid-migration conflict rollback (both schema and version), unknown schema rejection,
foreign key definitions, reference enforcement and metadata constraints are tested.
No production database was initialized or migrated during development.

The migration is forward-only: older v1-only binaries reject v2. No automatic
downgrade or full product backup capability is implemented. CI and combined
ASan/UBSan on a supported platform remain pending; no commit or release is claimed.
Next slice: normalized path policy, bound SQL index writer, injected timestamps,
scan status and conservative incomplete-scan reconciliation, followed by CLI index.
