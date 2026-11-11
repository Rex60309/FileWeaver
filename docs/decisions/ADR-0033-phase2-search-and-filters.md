# ADR-0033: Complete Phase 2 search modes and visible filters
Date: 2026-11-11
Status: Accepted by explicit full Phase 2 scope confirmation.

Append glob/path modes without changing existing enum values/defaults or JSON.
Glob is filename-only, case-sensitive Unicode-scalar matching with *, ?, groups,
ascending ranges and [!...] negation. No backslash escape; brackets quote literals.
Reject malformed groups/descending ranges and patterns over 256 scalars. A compiled
last-star matcher is bounded polynomial work and checks cancellation; do not use
SQLite's potentially long uninterruptible glob function. Filter/sort bound SQLite
records first, stream until matched result limit. Path is literal ASCII-folded
substring over indexed display paths, normalizing both slash styles in SQL.

Fuzzy --fuzzy-path permits tokens in individual parent components, never across
component separators. Filename scoring remains unchanged; each parent score is
capped by configurable path weight (default 50). Candidate cap/overflow errors,
UTF-8 validation, read-only snapshots, filters and deterministic tie rules remain.
Maximum path 32760 scalars/512 components; filenames 1024, query 128/16 tokens.
Share the existing strict scalar decoder via a Qt-free text helper.

GUI adds modes and visual CLI-equivalent extension/size/root/UTC-time filters,
sort/direction and a 1..1000 result limit (default 100). CLI retains 1..10000.
Fuzzy defaults to relevance; incompatible explicit sort/direction remains an error.
No new schema, index, content read, telemetry or dependency for search. Ordinary
B-trees are not claimed to accelerate arbitrary substrings/globs/fuzzy candidates.
Tests cover Unicode patterns/literal symbols, malformed inputs, cancellation,
filters/order/limit, path components, offline roots and existing mode contracts.

## Context, alternatives and consequences

Daily Search needs the authorized filename glob, literal path and parent-token
queries while preserving metadata-only search and deterministic ordering.
Alternatives were SQLite GLOB (less control over cooperative cancellation), regex
(extra grammar/runtime surface), or a new full-text schema (migration and changed
contracts). The chosen bounded matcher reuses bound filtering and snapshot reads.
It may scan/sort many metadata rows; no specialized-index performance is promised.
Adding enum members/options is an approved public search interface change.
