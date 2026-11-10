# ADR-0031: Bounded fuzzy filename search

Date: 2026-11-10
Status: Accepted; user explicitly approved shared core, CLI and Windows UI fuzzy
mode, typo/multi-token/subsequence matching, configurable deterministic ranking,
no schema change and no content reads.

## Decision

Append fuzzy/relevance enum values, preserving previous values/defaults. Fuzzy
requires relevance sort and no descending; CLI chooses relevance when fuzzy is
selected without --sort, GUI explicitly requests it. Other modes reject relevance.
CLI JSON shape/exit codes remain unchanged. Add FuzzyOptions at the end of the
request, candidate_limit default 4096 (1..10000), five weights 0..10000. CLI exposes
--fuzzy-{exact,prefix,token,subsequence,typo}-weight and --fuzzy-candidates. Windows
provides session-only settings, Cancel discards changes. No persistence/migration.

Use a Qt-free C++20 FuzzyMatcher in core. Decode strict UTF-8 into Unicode scalars,
fold ASCII only; no Unicode normalization/full case folding. Query at most 128
scalars, 1..16 ASCII-whitespace-separated tokens. Filenames at most 1024 scalars;
fail rather than truncate invalid/overlong text. Filename words split on ASCII
non-alphanumeric characters; non-ASCII text stays literal. Match filename only.
No path match, glob, activity collection, pinned/favorite data or external library.

Stream present filtered SQLite records from a read-only v2/v3 snapshot using bound
values. Stage one accepts every token that is a filename subsequence OR has a
possible whole-word typo. Typo candidate checks length and multiset bigram overlap:
shared >= max(lengths)-1-2*k (floor zero), plus multiset scalar overlap
>= max(lengths)-k; both are necessary Levenshtein conditions. The scalar bound
rejects disjoint short Unicode/ASCII words even when the bigram bound is zero.
No edit distance is computed for rejected candidates. Candidate overflow fails
with an actionable narrow-query/filter error, never silently truncates a shortlist.
Cheap false positives count against the budget. No B-tree/latency claim: scanning
and sorting filtered metadata may be linear or worse in index size.

Stage two computes bounded Levenshtein only for gated words: no typo for tokens
shorter than 3 scalars, one edit for length 3..5, two for 6..128. Banded dynamic
programming uses two bounded rows and cancellation checks. Transpositions cost
two edits; this is not Damerau-Levenshtein. All tokens must match; tokens need not
match distinct words or preserve query order. Default scoring: full folded-name
bonus 1000, full filename-prefix bonus 500, plus per-token best substring 300,
subsequence 100, or typo 50 minus edit count. Zero weights still permit matches.
Weights change ranking, not eligibility. Stable descending score order preserves
binary name/normalized-path/root-id/file-id tie breaks. Apply result limit last.

Reuse one GUI search worker, cancellation/stale-result rejection, SQLite progress
handler, 16 MiB candidate text budget and per-column limits. No new worker, data
model, source reads/writes, package, hidden collection or application network calls.
Windows-only manual focus; no release packaging or additional Linux work.

## Alternatives and consequences

FTS/trigram tables would need schema migration and synchronization; defer them.
Running edit distance on every filename violates the spec. A silently capped
shortlist could lose better results; explicit overflow trades broad-query utility
for predictable work and correctness. Users can narrow filters/query or use the
existing literal modes. Candidate scanning can still be costly on huge catalogs;
no universal performance target is claimed. Persistent preferences, path matching,
watcher and previews remain later milestones.

## Acceptance

Generated unit/SQLite/CLI/Qt tests for typo/token/subsequence/Unicode, q-gram recall,
filters, weights, stable ties, limits, cancellation, v2 compatibility, invalid input
and budget errors. Validate existing modes and single-JSON errors. Run repository
Windows Debug/Release build, CTest, format-check/lint gates and a reproducible
synthetic timing baseline. Sanitizers remain platform-limited; report evidence.
