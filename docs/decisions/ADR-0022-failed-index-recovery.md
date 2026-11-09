# ADR-0022: Synthetic write-failure and incomplete-scan recovery

Date: 2026-11-09
Status: Accepted under authorization to continue Phase 1 validation

## Context and alternatives

Successful incremental tests do not establish retention on a failed/incomplete
root. Filesystem permissions and process timing are unreliable fault sources on
Windows/admin environments. Use deterministic faults in an owned fresh fixture
database, reusing the existing integration-test trigger technique. This is not a
production schema/migration change and never accepts a user database/source root.

## Decision

Generate two small mixed fixtures, index them, then modify two A records, remove
one A file and create one replacement, plus modify one B record. In the temporary
database only, install a BEFORE INSERT trigger scoped to root A. A transactional
counter allows one changed-row write and aborts the second with a known marker.
Seeing that marker proves the first statement completed; after rollback the
counter must be zero and the prior A snapshot/IDs retained.

Verify a failing explicit index and a partial refresh. Refresh must retain failed
root A but commit healthy root B, as the existing per-root contract states; never
claim batch-wide atomic rollback. Remove all fixture fault objects and reindex A;
then verify new/updated/Missing counts and surviving/new IDs.

For the separate incomplete-return path, add one top-level fixture file and
temporarily set only A's saved maximum_depth to 1 using a bound, preconditioned
fixture UPDATE. Refresh must observe the top file, count blocked child directories,
roll back all A metadata, record incomplete and preserve the previous snapshot.
Restore the original depth and refresh successfully. Assert final fixture schema
version and absence of test-only objects; do not leave a fault outside the fixture.

Use existing 120 filter cases plus four targeted probes in six phases: 744 search
checks, full ID snapshots, roots/status inspection and scan/failure summaries.
Use public CLI JSON for product data; direct SQL is restricted to installing/
removing test faults and inspecting their counters/schema metadata, never editing
files/scans/root records as an expected-result shortcut. Every SQL parameter is
bound; DDL is static. Close connections before CLI calls.

## Consequences and limits

No product code/API/schema/dependency/matching change. All file mutations reuse
owned-temporary guards. New exclusive correctness reports contain labels/counts/
hashes, not paths. Tests cover the minimum fixture, invalid bounds, no-overwrite
and default richer Release run. No performance, disk-corruption/disk-full,
permission, cancellation, abrupt-kill or concurrent-race coverage is inferred.
Supported Linux/sanitizer runtime execution remains separate evidence.
