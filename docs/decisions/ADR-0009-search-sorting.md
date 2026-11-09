# ADR-0009: Deterministic search sorting

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1

## Context and alternatives

Search currently sorts filenames only. Presentation-side sorting after LIMIT would
return incorrect top results. Choose SQL sorting before LIMIT in the shared use
case instead of duplicating sorting in CLI or accepting arbitrary SQL fragments.

## Decision

Add SearchSort (name/size/modified) and descending=false to SearchRequest. CLI uses
--sort name|size|modified and --descending. Name ascending remains the default.
Validate enum values and select SQL identifiers/direction only from fixed internal
constants; never insert a user-provided sort string into SQL. Other values remain
bound. No schema, dependency or output-field changes.

The primary value sorts ascending unless descending is requested. Ties always
use ascending binary filename, normalized path, root ID then file ID, including
when primary direction is descending. Size means stored logical bytes; modified
means stored Unix-nanosecond modification time. Filtering precedes sorting and
LIMIT follows sorting. Search remains read-only; no current-clock logic is added.

## Consequences

Order is deterministic without promising locale-aware collation, file content
ranking or live source metadata. Sorting may require SQLite temporary storage;
existing returned-result budgets do not bound SQLite working memory. No performance
claim, new index or custom SQL sort is introduced. Existing stale-index and
overlapping-root behavior applies. Invalid modes reuse exit 2. Tests verify all
primary sorts/directions, stable size ties, filter-before-sort/limit and CLI errors.
