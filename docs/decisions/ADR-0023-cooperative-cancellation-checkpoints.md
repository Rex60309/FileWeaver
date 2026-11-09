# ADR-0023: Deterministic cooperative cancellation checkpoint tests

Date: 2026-11-09
Status: Accepted under authorization to continue Phase 1 validation

## Context and alternatives

Existing tests check cancellation flags but do not fully prove snapshots, IDs,
root statuses, recovery or per-root refresh boundaries. Sleep/poll races would
make small fixtures unreliable. Reuse scanner metadata callbacks and injected
application clocks to request stop at exact existing checkpoints, without adding
test hooks, threads, public interfaces or changing cancellation/commit semantics.

## Decision

Extend the existing C++ integration suite with tagged cooperative-cancellation
cases using its owned Fixture and public application inspection/search APIs.
Scanner tests request stop after a known number of metadata callbacks, verify no
further delivery/pre-cancel delivery, and rerun successfully on unchanged sources.

Index tests use an existing snapshot, then change/create/remove synthetic files.
Request stop before invocation or in the injected start clock. Verify zero scanned/
committed changes, cancelled history/root state, exact old tuples/IDs, no running
records and successful reconciliation/ID retention afterward.

Refresh tests stop before invocation, at the first root's start clock, or at its
finish clock. A pre-cancelled batch attempts no roots. Start-clock stop cancels
the first root and skips the second, preserving both snapshots. Finish-clock stop
arrives after that root's scan work is complete: the completed root may commit,
while the remaining root is not attempted. Preserve this existing checkpoint
contract; do not invent a late-stop guarantee or batch-wide rollback. Recover with
a new invocation/token and verify only outstanding changes are applied.

Check complete result tuples/IDs, flags/counters, policy/root state and scan history
through existing interfaces. Use fixed clocks and small owned temporary fixtures,
no process killing or timing thresholds. No CLI cancellation flag/Ctrl+C handler,
production source change, schema/dependency change or file mutation capability.

## Consequences and limits

Tests run as part of storage_tests and can be selected by cooperative-cancellation
tag. Format/lint and strict warning gates already cover that source. README states
the exact tested points and keeps CLI graceful Ctrl+C as unimplemented.
These cases do not deterministically cancel an index after rows have been staged
mid-scan; staged-write rollback is separately covered by failure/incomplete tests.
SQL progress cancellation, abrupt termination, OS signal integration, deadline or
cross-platform sanitizer coverage are not inferred. Keep supported CI pending
until actually executed.
