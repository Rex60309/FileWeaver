# ADR-0005: Serialized metadata indexing and conservative reconciliation

Date: 2026-11-08
Status: Accepted under full development authorization

## Context and alternatives

Schema v2 and the streaming scanner need a use case shared by CLI and future GUI.
Alternatives are per-file commits, an all-or-nothing scan with no durable status,
or a durable scan-start record followed by a transactional metadata update.

## Decision

`index_directory` takes an explicit database, root, scan options, injected UTC
Unix-nanosecond clock and stop token. `fileweaver index <root> --database <path>`
calls that use case. The database must already exist at schema v2; only `init`
creates/upgrades storage. New dependencies/schema changes are unnecessary.
The application-facing header contains only standard C++/scanner types. SQLite
details and the synchronous orchestration live in the existing storage adapter,
reusing its RAII/SQL error helpers. No database connections cross threads.

Validate roots/options first, then use a canonical root with lexical-normal UTF-8
file paths as exact keys. Do not case-fold or Unicode-normalize keys: Windows
case-sensitive directories must not collapse distinct records. Case variants of
root spellings can therefore produce separate roots; universal Windows path/file
identity deduplication is not claimed. Display paths retain their absolute spelling.
Root and file paths/names are bound parameters, never interpolated into SQL.

Persist root membership and a running scan record, then use one immediate
transaction for file writes, a temporary seen-key table, reconciliation and final
status. SQLite serializes writers with a bounded five-second busy timeout. This
holds a write transaction during I/O; it is a correctness baseline, not a speed
claim. The temporary table uses SQLite storage rather than an unbounded C++ set.

Compare size/mtime/presence before updating. Preserve indexed_at for unchanged
records. Changed/resurrected records are upserted and hash caches cleared. A rename
is a new path plus a missing old path, not an OS file-identity match. Metadata is
not content equality proof. Logical sizes, counts and UTC-nanosecond file timestamps
are range-checked before integer narrowing. Creation time remains NULL.

Database, WAL, SHM and rollback-journal paths are excluded automatically; database
hardlink aliases are also excluded using filesystem identity comparison. Explicit
exclusions apply relative to the root. Mark unseen present records Missing only
for a complete scan with zero skips and no configured user exclusions. A configured
exclusion defers reconciliation even if its path no longer exists. If any
exclusions/reparse entries are skipped,
commit observed metadata but defer all Missing reconciliation. This deliberately
retains potentially stale records. Prefer storing the database outside source roots.

Cancelled/incomplete scans roll back file writes, then persist their status with
zero committed added/updated/missing counts. Exceptions roll back file writes,
attempt to persist failed status and propagate diagnostics. A process crash may
leave a running scan record; it must not be interpreted as completion. This slice
has no automatic crash-history repair or multi-process job ownership protocol.
If status persistence itself fails, an error propagates rather than claiming success.

## Consequences

CLI prints counts and completion status; incomplete scans exit 1, invalid arguments
2, invalid roots/open permissions 3, absent index 4 and schema incompatibility 5.
No JSON/search/refresh command is introduced. Cancellation is available to C++
callers; graceful CLI Ctrl+C handling is deferred. The usual SQLite recovery of
uncommitted transactions applies on abrupt termination, without promising that
an interrupted scan is complete. Atomic multi-file filesystem snapshots, adversarial
replacement containment and universal root alias deduplication are not provided.

Tests must cover initial and repeated indexing, Unicode/quoted paths, modifications,
renames/deletions, incomplete/cancelled scan preservation, own-database exclusion,
failure rollback/status, and CLI success/invalid/uninitialized storage paths.
An embedded-NUL scanner regression was reproduced before its fix; roots and
exclusion paths now reject embedded NULs before filesystem use.
