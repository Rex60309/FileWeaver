# ADR-0019: Generated composable-filter correctness matrix

Date: 2026-11-09
Status: Accepted under authorization to continue Phase 1 validation

## Context and alternatives

Timing fixtures use zero-byte .txt files and do not exercise varied sizes/suffixes
or the interaction of filtering, sorting and limits. Add a bounded correctness
runner using existing CLI use cases, rather than another performance comparison,
changing production interfaces or using real file roots.

## Decision

Generate two disjoint Unicode roots with nested duplicate names, six suffix forms
(txt, TXT, md, tar.gz, no suffix, Unicode suffix), sizes 0/1/127/4096 bytes and four
fixed UTC mtimes. Accept 1..1024 files per root, default 72: at most 8 MiB of payload.
Use only owned temporary files/databases, no user-provided root. Reuse the existing
CLI JSON decoder and subprocess invocation, with explicit working directory/timeouts.

An independent fixture-record oracle applies query/extension/size/root/strict-time
predicates, stable primary sorting with ascending ties, then limit. It reads no SQL
or live files during verification. Check ordered name/path/size/root ID tuples and
positive unique file IDs. Run a fixed representative 120-case matrix online and
again after moving only the two owned generated roots to offline fixture paths.
Preflight both move endpoints inside the owned workspace with no destination
overwrite. Indexed results must remain unchanged; do not trigger a refresh.

Include exact inclusive bounds, final suffix versus multi-dot suffix, explicit
empty extension, ASCII-only case folding, root normalization/membership, reverse
primary sort with ascending ties, UTC-exclusive bounds, 1/10000 limits, exact
queries and SQL-like literals. Invalid argument cases require exit 2 and exactly
one JSON error. This is a representative matrix, not exhaustive Cartesian coverage.

Write a new exclusive correctness-only report after all checks. Record fixture
shape/counts, case configuration/phase/result counts, CLI/runner/helper hashes and
build info, with no temporary/real paths. Do not report latency, p95, speedup or
performance acceptance. Keep benchmark report versions and all prior evidence.

## Consequences

No domain API, schema, search rules, third-party dependencies or product CLI flags.
The new developer runner can run Debug or Release and is included as a tiny CTest
contract. It may move/delete only its own generated fixture directories, never
real sources. Any failure aborts without a success report; existing reports remain.
README and feature status reflect actual execution. Supported Linux/sanitizer CI,
controlled cold-cache and optimized-index work remain independently outstanding.
