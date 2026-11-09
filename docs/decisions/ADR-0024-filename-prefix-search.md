# ADR-0024: Literal filename prefix search

Date: 2026-11-09
Status: Accepted after user selected the proposed Phase 1 prefix milestone

## Context and alternatives

The next proposed product slice was --mode prefix, ASCII-case-insensitive literal
filename-start matching with existing filters/order/limits. The user asked to
continue after that proposal. Implement one shared application mode, not a CLI-only
filter or a new pattern language. No glob, fuzzy or path-search scope is selected.

## Decision

Append SearchMode::prefix after existing exact/substring values; preserve those
values and the substring default. CLI accepts exact|substring|prefix and maps the
selected value to search_files. Prefix selects the internal constant predicate
instr(lower(name),lower(?1))=1 with bound query/filter values. Do not concatenate
input into SQL or use LIKE/glob wildcards; %, _, quotes and stars remain literal.

ASCII case folds like current substring; other Unicode text is literal without
normalization/full case folding. Query must still be 1..4096 bytes without NUL.
Match the filename only, include present indexed records only, apply all existing
extension/size/root/strict-UTC filters before sorting/limit. Reuse cancellation,
read-only SQLite connection, schema v2/v3 acceptance, result budgets, tie-breaks,
exit codes, JSON shape and human output. No revalidation of live source files.

No schema/index migration or dependency. This function-based predicate may scan
indexed metadata; ordinary name B-trees are not claimed to accelerate it. Existing
performance reports do not establish prefix latency or a speedup.

## Verification and consequences

First run a CLI contract against the previous binary to observe rejection of the
new mode. CLI E2E exercises the shared application mode through the existing
generated filter oracle plus anchoring, ASCII/Unicode and literal-symbol probes,
default substring comparison, offline roots and unchanged catalog snapshots.
Follow-up regression coverage exercises prefix-mode v2 reads without migration,
Missing reconciliation/reappearance and pre-cancellation/retry. It does not claim
mid-query cancellation timing or CLI signal handling.
Extend only the private Python oracle's mode support; existing fixture/report
case counts remain unchanged. README/help/status must describe actual availability.
No source-file mutation, content collection, network access or broader phase work.
