# ADR-0032: Opt-in single-root Windows watch and reconciliation

Date: 2026-11-10
Status: Accepted; user explicitly selected Windows UI, one registered local root,
default off, background reconciliation, overflow/restart recovery, stop/status,
no service, source mutation or schema addition.

## Architecture and safety

One new persistent worker serializes catalog loading, native notifications and
single-root reconciliation; no per-file thread or parallel writers. GUI/search
remain independent. One pending command and one latest staged reply/queued wake,
generation-based stale delivery rejection. Stop cancels active work and queues a
stop acknowledgement; database changes invalidate old replies and clear selection.
Counters/last-success timestamp are carried through later status errors so a
coalesced successful commit still refreshes the visible search. Each worker use
case owns its SQLite connection; existing WAL, transactions, policy guards,
rollback and cancellation apply. A running scan may hold a write transaction;
external writers may cause timeout/retry. No new persistence or schema migration.

Explicit Load roots, select an existing known-policy root, then Enable watch.
Never auto-enable at startup/database selection; no implicit index creation or
full disk crawl. Stop/close ends the session; minimizing keeps explicitly enabled
monitoring running. No startup task/system service or saved preferences. Exclusions
and depth are reloaded from current saved policy for each refresh. New Qt-free
RootRefreshRequest/refresh_root use the existing refresh planner with a bound ID
and exact expected path; reject changed/removed registration before scanning.
Other roots are never scanned.
User explicitly approved retaining the existing conservative Missing rule: roots
with any exclusions/skips may retain stale old paths after delete/rename. Show
Missing-not-reconciled prominently rather than weakening scanner reconciliation. Unknown policy/v2 requires explicit CLI init/index.

Windows adapter uses one overlapped ReadDirectoryChangesW request, 32 KiB aligned
buffer, manual-reset completion event and cancellation event. Observe filename,
directory, size, write, attributes and creation changes; no last-access collection.
Ignore event payload names entirely: notifications are only dirty hints, not a
transaction log or proof of user activity. Rearm before scanning. Zero-byte/
ERROR_NOTIFY_ENUM_DIR completion requests immediate reconciliation. Any other
notification/scan failure closes the source, reports error and retries in 5 seconds;
every attach/restart reconciles. Root moves/deletion may be detected only by the
periodic scan because this API does not report the root itself.

A Qt-free monotonic schedule coalesces 500 ms quiet time with 2 s maximum deferral;
reconcile periodically every 60 s after successful scans, including when no event
arrives. UTC and monotonic clocks are injected. Periods are measured after scan
completion: no universal update-delay claim. Notification gaps/continuous changes
may still leave a transient stale snapshot; never promise perfect capture.

Require local available roots with no reparse/offline ancestor. Reject the index
path inside the watched root (supplied, canonical and handle-resolved volume-GUID
paths, covering short-path/drive aliases), and hard-linked/
reparse databases/sidecars to prevent self-triggered write loops. Revalidate before
every sync. Indexing retains its own DB exclusions and does not follow reparse
entries. Path checks and use are not atomic; malicious replacement is not solved.
Excluded subtrees can still produce dirty hints; only metadata from the authorized
scan scope is indexed. No content, raw notification path or access history is stored.
Existing scan-history records grow with each reconciliation; no retention change.

Cancellation uses stop tokens, CancelIoEx and drains the overlapped request before
freeing its buffer/OVERLAPPED. OS drivers may delay cancellation/close; no instant
shutdown guarantee. SQLite busy waits can also delay stopping. Completed commits
are not undone when stopping; cancelled/incomplete scans retain last valid index.

## Alternatives, acceptance and remaining scope

Do not apply raw notification events directly to SQLite or trust rename pairing.
Do not poll every file on the GUI thread, run refresh over all roots or use Qt's
watcher as a promise of recursive reliable delivery. Reuse conservative scanner
reconciliation. No new library. Multi-root watching, CLI watch commands, tray/service,
path search and previews remain separate milestones.

Generated tests: schedule boundaries/coalescing/backoff, selected-root policies and
identity/cancellation, actual Windows create/modify/rename/delete notifications,
reparse/DB-loop rejection, real temporary index synchronization, stopped changes/
restart, injected overflow/dropped-event periodic scan, disconnect/retry and GUI
opt-in/stale state. Never test against real user roots. Run Windows core/desktop
Debug/Release CTest/build/format/lint and record exact results/platform limitations.

References: Microsoft ReadDirectoryChangesW and CancelIoEx contracts:
https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew
