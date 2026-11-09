# ADR-0007: Composable metadata search filters

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1

## Context and alternatives

Filename search needs extension, logical-size and root restrictions without
duplicating rules in CLI/GUI. Alternatives are presentation-side filtering,
user-built SQL or optional bound predicates in the shared search use case.

## Decision

Add SearchFilters to SearchRequest with optional extension, minimum_size,
maximum_size and root. All default absent, preserving existing callers/output.
CLI exposes --ext, --min-size, --max-size and --root. Combine all active filters
with AND before deterministic ordering and LIMIT. Select SQL fragments only from
internal constants; every query/filter/limit value is bound. Reuse the read-only
connection, snapshot, cancellation and result budgets. No schema/dependency changes.

Extension compares the stored final suffix using SQLite ASCII-only lower().
Accept with/without initial dot. An explicit empty extension matches files whose
stored extension is empty; absence of a filter matches all extensions. For a
tar.gz file, the final suffix is .gz, not .tar.gz. Reject path separators, NUL and
more than 4096 input bytes. No wildcard or comma-list parsing is introduced.

Size bounds are inclusive nonnegative signed 64-bit logical byte counts. Reject
minimum > maximum. CLI accepts integer counts, not KB/MB suffixes. Zero and the
maximum representable size are valid bounds, without requiring an actual file
of that size. Filtering occurs on stored metadata, not a fresh filesystem stat.

Root selects exact registered membership via roots.normalized_path, not a path
prefix or all descendants in other roots. Resolve relative spelling against the
current working directory, lexically normalize dot components and remove redundant
trailing separators. Do not canonicalize/stat the filter path: a removed/offline
source must remain searchable. Use existing exact UTF-8/case-sensitive index keys;
different aliases/case spellings may not match. Empty/NUL paths are invalid, an
unregistered valid path returns no results. This filter never adds/removes roots.

## Consequences

Domain/API validation remains authoritative; CLI supplies typed values only.
Invalid inputs reuse exit 2; existing result format/ordering/other exit codes stay
unchanged. Optional OR predicates may affect query plans; no acceleration or
latency claim is made. Modification-time filters, custom sorting, glob/fuzzy
matching, JSON and benchmarks remain separate work.

Tests cover case/dot normalization, final suffix, empty-extension and zero-byte
cases, exact inclusive boundaries, combinations before limit, invalid ranges,
SQL-like literals, exact root membership, relative/trailing-dot/offline roots,
unregistered roots, and CLI success/invalid/missing-value/overflow cases.
