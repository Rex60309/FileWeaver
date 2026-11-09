# JSON CLI verification

Date: 2026-11-08
Scope: Phase 1 single-document JSON for init/index/search/help and failures.
Environment: Windows GCC 13.2.0, Python 3.13.9; existing production dependencies.
Decision: ADR-0010-json-output.md. Schema remains v2.

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed; Python >=3.9 discovered for tests |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 3/3 including strict JSON contract |
| `ctest --preset debug --output-on-failure -R cli-json-contract` | Passed, 1/1 after authorized permissions retry |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 43 cases / 275 assertions |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed after adding final exception boundary |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 3/3 |

## Acceptance evidence

Independent Python stdlib parsing decodes strict UTF-8 and validates the entire
stdout (rejecting trailing documents/garbage), duplicate object keys and nonstandard
numeric constants. It exercises global flag before/after commands, bare JSON help,
subcommand help, initialization, initial/unchanged indexing, filtered/sorted search,
empty results, warning isolation and exact signed-int64 maximum size preservation.
Errors 1..5 are exercised via runtime write failure/invalid stored UTF-8, invalid
arguments/date/limit, inaccessible parent, absent storage and corrupt SQLite.
Literal --json after the -- delimiter and an attached extension value do not enable
mode. Explicit false override is rejected. Human-output E2E checks remain intact.

C++ tests cover control/quote/backslash escaping, Unicode including four-byte
characters, invalid leading/continuation bytes, overlong/truncated/surrogate and
out-of-Unicode-range UTF-8, and incomplete/cancelled index summary fields. The CLI
write-failure test uses a trigger in a synthetic database, never real user data.
Python optimization is rejected so assertions cannot silently be disabled.

Initial restricted JSON test execution could not create/remove a Unicode fixture
under the execution environment's temporary directory (WinError 5). The same test
passed with authorized permissions; tests were not skipped or assertions weakened.
Initial lint found an exception could escape from early/help serialization; the
CLI now has a final exception boundary. No analyzer checks were disabled.

## Safety and limitations

No schema/migration, new production dependency, telemetry, content indexing,
network downloads or source-file mutation was introduced. Python >=3.9/json/sqlite3
is a development/test-only dependency. All fixture sources/databases are generated
in isolated temporary directories and cleaned by their owners.

Invalid UTF-8 paths cause JSON runtime errors, not replacement or partial results.
Integers are emitted exactly; consumers using binary64 numbers may lose int64
precision. Serialization can expand escaped text within existing bounded search
result sizes. Output-device/OOM failures cannot guarantee delivery. Incomplete CLI
scan payload is covered at serializer level; no filesystem ACL/in-flight cancellation
stress was run. Existing scan race, stale-index and root alias limitations remain.
CI/supported-platform ASan/UBSan remain pending. No commit/release/performance claim.
Next smallest slice: refresh of explicitly registered roots, then reproducible
core-search benchmark before claiming Phase 1 completion.
