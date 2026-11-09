# ADR-0003: Bounded synchronous metadata scanning

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1 implementation

## Context and alternatives

Indexing needs a tested scanner before persistent scan state and missing-file
reconciliation can be introduced safely. Alternatives are returning all metadata
in memory, a worker pool, or synchronous callback streaming.

## Decision

Use a synchronous streaming callback with standard C++20 types and stop_token.
No worker pool or SQLite dependency is introduced. The caller chooses the root;
empty, inaccessible, non-directory, symlink/reparse roots and roots reached through
linked ancestors are rejected. Windows reparse detection stays in the filesystem
adapter and rejects all reparse tags, including junctions and cloud placeholders.
No links are intentionally followed. Source contents are never opened.

Use at most 128 active directory iterators (configurable to a smaller positive
limit); reject zero or values above 128. Prune deeper directories, increment
errors/skipped, and mark the scan incomplete. Metadata includes original absolute
path, logical size and filesystem modification time. Creation time is deferred to
a platform metadata adapter; no timestamps are inferred or persisted here.

Explicit exclusion paths match an entry or selected root; excluding a directory
prunes its subtree. Relative exclusions are relative to the selected root. Windows
uses case-insensitive ordinal comparisons, other platforms exact comparisons.
Symlink/reparse and exclusion skips are intentional; I/O errors and depth pruning
set complete=false. Cancellation is checked before traversal and between entries.
Consumers must never interpret incomplete/cancelled scans as proof of deletion.
Sink exceptions propagate, and RAII closes iterators. Traversal order is unspecified;
future search output must sort explicitly. No clock or wall-time logic is needed.

## Consequences and limits

There is no CLI scan/index command or database migration in this slice. The future
index use case must supply its own database/cache exclusions, persist scan status,
and perform transactional reconciliation. Exclusions are not an OS sandbox.
Filesystem status checks race with directory replacement; this path-based portable
scanner cannot guarantee containment against malicious concurrent replacement.
Handle-relative traversal would be a separate platform capability. Cancellation
cannot interrupt an individual blocking OS filesystem call or a blocked sink.
Windows UNC/device/extended namespace roots are explicitly rejected before any
filesystem access in this slice. An attempted extended-path depth fixture stalled
in this environment; no long-path support is claimed. Depth-limit behavior is
tested with a smaller configured bound using ordinary synthetic paths.
