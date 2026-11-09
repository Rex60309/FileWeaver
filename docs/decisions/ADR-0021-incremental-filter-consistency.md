# ADR-0021: Incremental filter snapshot consistency

Date: 2026-11-09
Status: Accepted under authorization to continue Phase 1 validation

## Context and alternatives

ADR-0019 verifies one indexed snapshot online/offline. It does not prove that
filtering, ordering, IDs and counters remain consistent after controlled changes
and reconciliation. Reuse its generated fixture/independent record oracle rather
than add another scanner/search implementation or mutate real sources.

## Decision

Add a correctness-only developer runner with two generated roots, 3..1024 files
per root. Reuse fixture records and 120 cases, plus four targeted exact probes.
Run five phases: initial, changed-on-disk but stale index, explicit reindex,
unchanged refresh, and refresh after restoring original paths. Check 124 cases
per phase and full present-path/ID snapshots plus aggregate status/scan counters.

Inside the owned temporary workspace only, update one size/mtime, change one mtime
without changing size, remove one file, rename one to a Unicode suffix, and create
one file. Before rescan, search must still return old indexed metadata. Full index
must report two added/one updated/two Missing in root A and one updated in root B.
An unchanged refresh must report zero changes. Restore the two original paths,
remove the new path, and refresh: two existing Missing records are updated back
to present with original IDs, while the two temporary paths become Missing.

Check every mutation endpoint resolves inside the owned TemporaryDirectory and
refuse unexpected existence/collisions before create/rename/remove. These are
synthetic test operations, not a product file-management feature or user-file
undo guarantee. Never accept a caller source root. No recursive real-data deletion.

Extract the existing matrix's query/result validation into shared private helpers.
Compare complete ordered name/path/size/root-ID tuples against fixture records,
and verify stable file IDs on surviving paths; renamed destinations get new IDs,
resurrected original paths retain their original IDs. Use status/roots/index/
refresh public JSON; do not read or modify database tables directly.

Write a new exclusive correctness report only after all phases pass, including
source/build hashes, cases and counter summaries without temporary/personal paths.
No timing or performance claim. No domain API, schema, matching rule, product CLI
flag, third-party dependency or source-file mutation capability is introduced.

## Consequences

CTest covers the minimum 3-file-per-root boundary, invalid 2/1025 bounds and report
no-overwrite. The default richer fixture is recorded separately. Existing filter
matrix/empty-option regressions remain. No supported-sanitizer/cross-platform
execution is inferred from Windows passes. Incomplete/error/cancelled scan recovery
and concurrency remain independently testable work; do not weaken reconciliation.
