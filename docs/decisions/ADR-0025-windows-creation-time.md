# ADR-0025: Available Windows creation timestamps

Date: 2026-11-09
Status: Accepted; user explicitly approved filling the Phase 1 SPEC 3.2 gap

## Context and alternatives

The existing writer deliberately left created_at NULL (ADR-0005), although SPEC
requires creation time when supported. The user selected completing Phase 1 and
approved Windows native metadata reads without schema changes. Do not infer
creation from modification time or add a cross-platform dependency/public API.

## Decision

Keep nullable created_at and schema v3. In the storage/filesystem adapter boundary,
open a Windows metadata-only handle with FILE_READ_ATTRIBUTES and sharing for read,
write and delete; FILE_FLAG_OPEN_REPARSE_POINT avoids following the final link.
Use RAII to close the handle. Read native creation FILETIME, rejecting directory
and reparse records. Convert 100ns ticks since 1601 to signed Unix nanoseconds with
explicit overflow bounds. Unavailable/unrepresentable values remain unknown.
This does not provide handle-relative containment against ancestor replacement.

Compare available creation timestamps during incremental indexing. Backfill a
previously NULL value on an explicit scan, counting it as one metadata update;
unchanged subsequent scans retain indexed_at. Preserve an already known creation
time if a later read is unavailable. New records on unsupported platforms remain
NULL. This supersedes only ADR-0005's unconditional-NULL creation-time choice.

The product reads attributes only, never file contents or source writes. No
SQLite migration, public C++ structure change, CLI/JSON field or new dependency.
No Windows handle or connection crosses threads. The existing index transaction
and rollback/cancellation rules continue to apply.

## Verification and consequences

First reproduce absent creation time with a failing Windows regression. Compare
persisted nanoseconds with an independent GetFileAttributesExW observation on a
generated Unicode file; test NULL backfill and unchanged rescan. On Linux assert
NULL rather than fabricating birth time from mtime. Rerun full Windows/Linux
Debug/Release and sanitizer gates, and current-binary 100k evidence afterward.

One extra metadata handle read per regular file on Windows can affect indexing
throughput. Creation time describes filesystem metadata, not user activity, and
may be changed by other programs. It is not file identity or content equality.
