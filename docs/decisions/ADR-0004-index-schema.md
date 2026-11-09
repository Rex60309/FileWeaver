# ADR-0004: Transactional metadata index schema v2

Date: 2026-11-08
Status: Accepted under full development authorization

Extended by ADR-0011: current storage is v3, adding explicit root scan policies.
The v2 index-table design below is retained as migration history.

## Context and alternatives

The streaming scanner is tested but has no durable index. Keeping an in-memory
index loses state, while adding tables outside a migration can leave partial
upgrades. Use a versioned transactional migration before connecting the writer.

## Decision

Initialization creates or upgrades to v2 in the existing immediate transaction.
Validate the v1 settings schema first; preserve its contents. Add roots, files
and scans corresponding to SPEC sections 18.1 and 18.2. Reject unknown versions.
Any creation/validation failure rolls back tables, indexes and user_version.
Reopening v2 verifies expected columns without rewriting metadata records.
There is no destructive down-migration and no overwrite of unrelated tables.

Root paths are unique; files are unique per root/path, allowing overlapping roots
to retain their own scan membership. All references use foreign keys, with no
implicit cascading deletion. Sizes and counters must be nonnegative, presence
must be boolean, statuses are constrained. Only regular-file metadata is modeled.
Nullable creation time means unsupported/unavailable, never an inferred time.
UTC timestamps are integer Unix nanoseconds in the future writer; that writer
must accept an injected clock and validate narrowing of file sizes/timestamps.
Hash columns remain NULL until separately authorized hashing is implemented.

Ordinary name B-trees are for equality/prefix strategies where applicable; they
do not promise acceleration of arbitrary substring queries. Additional indexes
support per-root presence and scan history. Future writer/search queries must
bind all user values. Path normalization and Unicode comparison policy must be
recorded with the writer, before path strings are actually persisted.

## Consequences

`init` now upgrades v1 storage. Older v1-only binaries reject v2; migration is
forward-only and no automatic rollback to an older binary is promised. Existing
settings are preserved. Fresh, upgrade, repeated-open, foreign-key/constraint and
rollback tests are required. No source scanning or CLI index/search behavior is
introduced by this step. The index writer and CLI integration remain next work.
