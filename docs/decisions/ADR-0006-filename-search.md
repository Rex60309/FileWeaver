# ADR-0006: Bounded read-only filename search

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1

## Context and alternatives

The persistent index needs a small useful search capability before advanced
filters/fuzzy ranking. Alternatives include filesystem scans per query, LIKE
patterns requiring wildcard escaping, or bound exact and literal substring SQL.

## Decision

`search_files` exposes standard C++ value types. It opens the explicit database
with SQLITE_OPEN_READONLY and checks schema v2 in the same read transaction as
the query. It does not initialize/migrate storage or revalidate live source files.
SQLite WAL support can still require SQLite-managed sidecar access; application
SQL performs no writes. Reuse the existing storage adapter's RAII and bound-SQL
helpers; presentation renders the returned values only.

CLI: `search <query> --database <path> [--mode exact|substring] [--limit N]`.
Default substring uses instr(lower(name),lower(?1)); SQLite lower folds ASCII only.
Unicode is literal without normalization/full Unicode case folding. Exact uses
binary filename equality. `%`, `_`, slashes and apostrophes have no special query
syntax. No SQL is constructed from query text. Existing name B-tree can support
exact equality; arbitrary substring search may scan the index. No speed claim.

Only is_present=1 records qualify. Sort by binary filename, normalized path,
root ID then file ID before limit. Overlapping roots may return duplicates.
Empty/no-match searches are distinct: empty query is invalid; no matches succeeds
with empty stdout. Missing records may remain if indexing deferred reconciliation.

Limit defaults to 100 and accepts 1..10000. Query length is 1..4096 bytes without
NUL. Returned text fields are at most 131072 bytes each and total returned text
is capped at 16 MiB; overflow fails rather than silently truncating results.
Result memory is bounded by both these limits and the count limit. C++ callers
can cancel via stop_token; check before open/between rows, with a SQLite progress
handler every 1000 VM instructions. Cancellation throws and exposes no partial
result vector. OS locking calls may still wait up to the five-second busy timeout.

Human output is one logical-byte size, tab, quoted UTF-8 display path per row.
Quote/backslash and ASCII controls/DEL are escaped to keep paths on one line and
avoid raw terminal control bytes. This is human text, not a JSON output mode.
Diagnostics use stderr. Exit codes reuse 1 runtime/cancel, 2 argument, 3 open or
permission, 4 uninitialized, 5 schema/corrupt metadata. No new schema/dependencies.

## Consequences

Stale metadata is visible until successful reconciliation. No content reads,
file opening, fuzzy/glob/path search, root/size/time filters, JSON or CLI graceful
Ctrl+C are introduced. Large-index latency and concurrent-reader performance
need benchmarks and concurrency tests later. No portable GUI threading is added.
Tests cover literal special characters, Unicode, casing, deterministic ties,
limits, Missing exclusion, absent/corrupt storage, cancellation and CLI contracts.
