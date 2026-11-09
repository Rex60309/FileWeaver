# ADR-0011: Refresh registered roots without losing scope policies

Date: 2026-11-08
Status: Accepted under full implementation/schema authorization

## Context and alternatives

V2 stores root paths but not user exclusions or scan depth. Refreshing those paths
with defaults could collect metadata the user previously excluded. Alternatives
are guessing defaults, refusing all refresh forever, or versioned policy storage
with explicit confirmation for legacy roots. Choose the latter.

## Decision

Schema v3 transactionally adds root_scan_options(root_id PK/FK,maximum_depth 1..128)
and root_exclusions(root_id FK,ordinal>=0,path nonempty, PK(root_id,ordinal)).
Initialization upgrades v1/v2 to v3 with settings, roots, file metadata and scan
history preserved. Failure rolls back tables/version. No downgrade is provided.
V2-only binaries reject v3. New search accepts v2/v3 read-only; index/refresh require
v3. Migrated roots have no policy row, explicitly meaning unknown, not empty.

An explicit index invocation saves/replaces its supplied exclusions/depth in the
same transaction as its running scan record. Omitting exclusions on a new index
invocation explicitly replaces them with none. Database/sidecar auto-exclusions
are recalculated per execution and not saved as user policy. Failed/cancelled
metadata scans can retain the explicitly requested policy; metadata still rolls
back. Preflight failures before scan registration do not replace policies.

`refresh_index(database,clock,stop)` snapshots registered roots and policies in
binary-path/root-ID order, releases its read connection, then invokes the existing
index use case serially. No roots are discovered or implicitly registered. Unknown
policy roots are reported with exit 5/attempted=false and must first be explicitly
re-indexed with their desired exclusions. Empty catalogs succeed without scanning.

Refresh requests carry expected root IDs. The writer verifies root identity and
exact saved policy before replacing policy/start records, and again under the
metadata writer transaction immediately before traversal. Changed policies abort
that root instead of overwriting a newer privacy restriction. SQLite serializes
writers; this is not a general multi-process job-ownership protocol or protection
against malicious filesystem path replacement.

Bound inventory to 10000 roots, 10000 total exclusion paths, 131072 bytes per path
and 16 MiB total path text. Oversized/invalid inventories fail before scanning.
Individual index policies use the same count/per-path/text bounds. SQLite working
memory is not completely bounded by these returned-plan limits.

Continue after individual root failures. Each root commits independently, so a
failed batch can contain successfully updated roots. Missing/inaccessible roots
retain prior metadata; preflight failures are reported in the refresh result but
do not invent a completed scan-history entry. Cancellation preserves completed
root results and marks remaining roots unattempted. In-progress cancellation uses
the existing index rollback/status behavior. Whole-refresh rollback is not promised.

## CLI and JSON

`refresh --database <path> [--json]`. Human output includes each root/code/attempt
and available index counts, plus final root count/status. Warnings/details use
stderr. Any per-root failure/cancellation yields overall exit 1; initial catalog
errors use existing codes. JSON uses ok,exit_code,command="refresh",cancelled,roots.
Each root contains root_id,path,attempted,exit_code; failures add error, available
index summaries add index (the existing full index JSON object). Overall failure
adds error="Refresh incomplete". Unknown policies are never silently skipped.

## Consequences and evidence

Schema migration and public JSON changes are explicit, opt-in through init.
No new dependencies, source mutation/content reads or network behavior. Tests
cover fresh/v1/v2 creation/upgrade, v3 rollback, unknown-policy guard, exclusion
and depth reuse, mixed success/failure, between-root/pre-requested cancellation,
policy changes after snapshot, inventory cap, v2 read compatibility, and human/
strict JSON contracts. Existing source race, stale-index and graceful CLI Ctrl+C
limitations remain. Benchmark/CI/sanitizer work is still required.
