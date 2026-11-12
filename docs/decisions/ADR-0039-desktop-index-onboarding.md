# ADR-0039: Desktop folder-first index onboarding

Date: 2026-11-12
Status: Accepted by explicit user confirmation; implemented and locally tested on Windows GCC; hosted CI pending.

## Context

The user reports that selecting SQLite, manually initializing/scanning with CLI
and seeing a blank view until entering a query makes daily use impractical.
Prior Windows Phase 2 slices implemented search over existing indexes. This
proposal adds creation from the desktop without expanding into file management.
The user explicitly approved managed Windows application-local storage and the
background writer, bounded browsing and cancellation described below.

## Authorized user flow

1. Make Add search folder the primary action; retain existing-index selection.
2. User chooses a local folder, reviews the scope and starts metadata indexing.
3. Store the managed catalog below Qt AppLocalDataLocation, separate from any
   existing user-selected SQLite file. Do not replace/move the existing index.
   The user selected managed storage; arbitrary new catalog locations are not
   exposed in this slice. Existing-index selection remains available.
4. Initialize and scan on one bounded background worker with visible stage status,
   cancellation and final file/skipped/error counts. Unknown totals use an
   indeterminate indicator, not a fabricated completion percentage.
5. On success, open the resulting catalog and display a bounded first page of
   files (default 100); typing a query narrows results. Existing-index selection
   also shows files without requiring the user to know the glob wildcard.
6. A previously created managed catalog may reopen read-only on startup, without
   rescanning, monitoring or reading file contents automatically.

## Implementation boundaries

Reuse initialize_database/index_directory and the existing search use case; no
schema migration, content index, new dependency or external helper command.
Use an injectable clock and adapters following existing desktop worker patterns.
One active indexing job; reject duplicate submissions. Prevent competing watcher
writes and index changes during this job. All SQLite connections stay on their
own worker/use-case threads. Cancellation retains the last valid metadata via
existing transaction behavior; a newly initialized empty catalog may remain for
retry. Never delete a catalog as an automatic cancellation cleanup.

Validate local roots/storage with guarded OS/filesystem adapters; do not follow
reparse points or crawl implicit home/full-disk roots. Keep managed storage
outside the selected root; reject a containing root with an actionable message. Preserve original display paths and existing Missing reconciliation
rules. Watch, preview and hotkey activation remain separately opt-in.

Empty UI query browsing maps internally to a bounded glob request through the
shared search API. Do not loosen the CLI/core empty-query contract. Document
browse sorting and filter behavior, and cancel stale browse/search results.

## Acceptance and alternatives

Use temporary generated directories only; inject catalog paths so tests cannot
write real AppData or scan real-user folders. Cover Unicode, first creation,
existing catalog/source preservation, invalid/unwritable paths, cancellation,
errors, repeated submission, bounded first-page display and responsive UI.
Run focused tests, standard core/desktop Debug/Release, format/lint and applicable
sanitizer/CI checks; update README and feature status to actual behavior.

Keeping CLI setup as the primary flow does not address the reported problem.
Explicit catalog selection remains an alternative for advanced users, not a
prerequisite for ordinary use under the approved managed location. Multi-root
simultaneous watching, new preview formats, later phases, Linux work and release
packaging are outside this proposed slice.

## Implementation safeguards

Case-insensitive spelling reuse additionally requires filesystem identity equivalence;
a differently cased physical directory cannot redirect the scan. Search pauses during
the writer job. Existing zero-byte managed catalogs can be initialized only without
sidecars; orphan sidecars are rejected and never deleted automatically. Existing
nonempty catalogs require schema v3. See ../index-onboarding-verification.md for
regression evidence and the current acceptance status.
