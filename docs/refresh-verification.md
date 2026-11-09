# Saved-policy refresh verification

Date: 2026-11-08
Scope: Phase 1 schema v3 policy persistence and registered-root refresh.
Environment: Windows GCC 13.2.0 / Python 3.13.9, existing dependencies.
Decision: ADR-0011-refresh-policies.md.

| Exact command | Observed result |
| --- | --- |
| `cmake --preset debug` | Passed with existing compiler/vcpkg configuration |
| `cmake --build --preset debug --parallel` | Passed |
| `ctest --preset debug --output-on-failure` | Passed, 3/3 including JSON refresh/upgrade cases |
| `build/debug/storage_tests.exe --reporter compact` | Passed, 50 cases / 334 assertions |
| `build/debug/storage_tests.exe 'Refresh*,V2 upgrade*,V3 migration*' --reporter compact` | Initial six-case slice passed, 49 assertions; later expanded with cancellation/depth coverage |
| `cmake --build --preset debug --target format-check` | Passed |
| `cmake --build --preset debug --target lint` | Passed after checked optional test access |
| `cmake --preset release` | Passed |
| `cmake --build --preset release --parallel` | Passed |
| `ctest --test-dir build/release --output-on-failure` | Passed, 3/3 |
| `build/debug/fileweaver.exe refresh --help` | Passed; explicit database and JSON options |

## Acceptance evidence

Fresh/v1 creation now reaches v3. Synthetic v2 metadata/settings are preserved
through upgrade; search can read v2 without migration, while refresh requires v3.
Migrated unknown-policy roots are refused without scanning until explicit index
confirmation. Conflicting policy-table creation rolls back new tables/version;
reinitialization succeeds after removing the synthetic conflict.

Saved exclusions prevent collection of a private fixture and unregistered roots;
saved depth pruning remains incomplete rather than silently expanding traversal.
Mixed missing/healthy roots report distinct results, retain inaccessible-root
records, and update healthy-root additions/deletions. Pre-requested and between-root
cancellation retain completed results and identify unattempted roots. An injected
clock callback changes stored policy after snapshot; refresh rejects that root
without collecting the newly excluded fixture. Root inventory >10000 fails before
any scan record is created. Plan/path limits are implemented; aggregate exclusion/
text-budget stress fixtures were not executed.

Strict JSON E2E covers empty/success/partial refresh, nested index summaries,
saved missing-reconciliation policy, absent storage and the full v2 upgrade/
unknown-policy/explicit-reindex flow. Human CLI refresh also passes. Tests use
synthetic temporary directories/databases only; full suites run with authorized
permissions for existing junction fixtures. No tests are skipped.

Initial lint identified unchecked optional summaries in tests; a shared checked
test accessor now fails explicitly when a summary is absent. Assertions and
production validation were retained, without adding analyzer suppressions.

## Safety, compatibility and remaining work

This slice introduces an explicit transactional schema v3 migration, two policy
tables and additive refresh API/JSON contracts. Old v2-only binaries reject v3;
there is no automatic downgrade. Init is the explicit upgrade operation. Index
replaces saved user policy with its supplied options, including empty exclusions
when omitted. Refresh never guesses legacy policies or discovers new roots.

Only index metadata/policy/history is written; no real-user indexing, source file
mutation/content reads, new dependencies or network downloads occurred. Each root
commits independently; no whole-batch atomicity is promised. Preflight failures
are reported but do not create fabricated scan history. Snapshot policy comparison
is tested deterministically; multi-process stress/job ownership, malicious filesystem
replacement containment and graceful CLI Ctrl+C remain unimplemented/unverified.
Root aliases, skipped-entry stale records, SQLite resource and invalid-UTF8 JSON
limitations remain. CI and supported-platform ASan/UBSan have not run. No commit,
release or performance claim is made. Next: read-only root/status inspection, then
reproducible search benchmark before claiming Phase 1 completion.
