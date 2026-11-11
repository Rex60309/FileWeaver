# ADR-0035: Launcher hide behavior and optional local preferences
Date: 2026-11-11
Status: Accepted by explicit Phase 2 completion confirmation.

When global hotkey is enabled Escape hides the window, retaining its registration
so the hotkey can reopen/focus it. X still quits/stops workers. Escape without an
enabled hotkey still closes. Preview is disabled/cleared when hiding/minimizing;
metadata watch continues only if explicitly enabled. Existing open confirmation
and executable/script restrictions stay; no Phase 4 application/project launcher.

Optional Remember preferences uses one local JSON version 1 file under Qt's
AppLocalDataLocation/FileWeaver namespace, QSaveFile atomic replacement and one
bounded background loader/writer. Missing file means defaults and no write.
Only affirmative opt-in saves values: weights/candidate cap, hotkey combination,
font, search mode, sort/direction and result limit. No query, DB/root/file paths,
contents, thumbnails, activity or hashes. Never persist/restore hotkey activation,
watch or preview. Unchecking writes a disabled marker instead of retaining old
values. Report I/O/format failures and leave defaults on malformed/oversized input.
Max 4 KiB, strict version/ranges; no silent migration of unknown versions. No DB
schema change. Final pending save drains on shutdown; local storage can delay close.

The main entry point explicitly supplies the application preference path; default
SearchWindow unit instances never read/write a real profile. Tests explicitly use
generated temporary files. Verify off defaults, round-trip, corruption/ranges,
disabling, cancellation/stale reply handling, hotkey focus and Escape/X semantics.

## Context, alternatives and consequences

A launcher hidden by Escape needs an explicitly enabled way to return; without
a hotkey it must close normally. Always hiding could leave an unreachable window;
always closing defeats the active hotkey workflow. Optional preferences avoid
repeating harmless choices while retaining per-launch privacy consent. Registry
settings were considered; one versioned JSON file is inspectable and testable.
Persisting the last query/root or activation would silently restore collection
scope and was rejected. Atomic replacement protects the prior committed file on
write failure, but cannot promise survival of all filesystem/power failures.
