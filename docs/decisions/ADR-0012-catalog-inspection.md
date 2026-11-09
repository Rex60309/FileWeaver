# ADR-0012: Read-only catalog roots and status

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1

## Context and alternatives

Users need to inspect registration/policy before refresh and understand indexed
record counts. Scanning sources during inspection would change metadata semantics
and prevent offline-root review. Choose read-only catalog snapshots, not live
filesystem status or a new schema/cache.

## Decision

Add list_roots and catalog_status use cases with standard C++ result types and
optional stop tokens. Share the existing storage adapter's connection/statement
RAII. A common helper opens SQLITE_OPEN_READONLY, starts a read transaction and
accepts schema v2/v3 without migration. No source paths are stat'ed or scanned.
SQLite may still need its WAL/SHM sidecars. Use five-second busy timeout and a
progress callback every 1000 VM instructions, plus explicit cancellation checks.
Connection destruction ends the read snapshot; no write SQL is issued.

Roots sort by binary normalized path then root ID. Report root_id, path (the exact
indexed normalized key), recorded last_scan_status, nullable last_scan_at in UTC
Unix nanoseconds, policy_known, nullable maximum_depth, and saved exclusions in
ordinal order. V2 or v3 roots without policy are unknown: false/null/empty array,
which must not be interpreted as confirmation of no exclusions. Do not guess
default policies. Validate stored text/types and known status values.

Bound roots to 10000, total exclusions to 10000, each text field to 131072 bytes
and returned text to 16 MiB. Fail instead of silently truncating. Status counts
use int64 SQLite counts, independent of the root-list result cap. Count roots,
present_files, missing_files, scan_records and running_scan_records. These are
indexed records, not unique physical files. Running rows may be interrupted
processes, not live jobs. Status is not an integrity checker or event watcher.

CLI: roots/status --database <path> [--json]. Human roots output lists quoted paths,
policy/depth and recorded status/time, then exclusions and total roots. Human
status prints labeled counts. JSON preserves the common ok/exit_code/command
envelope: roots adds roots array/count; status adds schema_version and the five
counts. Null times/depths remain null. Invalid UTF-8 uses the existing JSON failure
behavior; serialize completely before output. Existing exit categories apply.

## Consequences

No schema/dependency, collection, mutation or live-job claims. Old metadata remains
visible for missing/offline sources until reconciliation. Overlapping roots can
count a physical file multiple times. SQL counts may scan large tables; no speed
claim or SQLite working-memory cap is introduced. Tests cover empty/populated,
offline roots, policy details, recorded running/missing counts, v2 preservation,
cancellation, root/text validation/caps, and human/strict JSON contracts.
