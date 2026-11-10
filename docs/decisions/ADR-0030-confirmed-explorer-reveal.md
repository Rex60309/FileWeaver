# ADR-0030: Confirmed Windows Explorer reveal

Date: 2026-11-10
Status: Accepted; user explicitly approved confirmation and locating all extensions
(including programs, scripts and shortcuts), with no file execution or relaxation
of the existing opening allowlist.

## Scope and alternatives

Continue the Windows Phase 2 result-navigation slice. Add an explicit Show location
button and result Ctrl+Enter; preserve Enter/double-click as confirmed file opening.
No file mutation, folder-management operation, watcher, schema, content indexing,
clipboard collection, release packaging or additional Linux work.

Use SHParseDisplayName followed by SHOpenFolderAndSelectItems with one absolute
file PIDL, cidl=0 and flags=0. Windows opens its parent and selects the item.
Do not construct an explorer.exe command line, invoke the file's open verb,
resolve a shortcut target, request rename mode or call a file association.
Reuse COM STA/system Shell32/Ole32 dependencies. PIDL memory is released by RAII.

## Safety and execution

Separate the shared local-path syntax policy from the file-opening extension
allowlist. Reveal accepts all extensions but still requires an existing local
regular file and rejects URL/UNC/device/ADS paths, offline/remote storage and
reparse points in the file or ancestors. Missing items fail rather than silently
opening a different folder. Opening retains its existing allowlist unchanged.
Both operations show their exact immutable path in a plain-text scrollable
confirmation; Cancel is default. No action occurs on mere selection or search.

The existing FileOpenController gains a typed action and a second injectable
handoff callback. It retains one worker, one accepted operation, no waiting queue,
backpressure and explicit cancellation. No additional worker is introduced.
An outstanding open blocks reveal and vice versa. OS shell calls may block and
cannot be recalled after handoff; path inspection and PIDL parsing are not atomic.
Shell acceptance is not proof a selected item became visible or foreground.
Explorer can load its own previews/extensions; FileWeaver does not promise a
sandbox or control subsequent actions manually performed in Explorer.

## Acceptance

Fake handoffs only in CTest. Cover Unicode paths, unsupported-for-open extensions
accepted only for reveal, cancellation/default rejection, immutable snapshot,
wrong-action dispatch, shared busy state, missing/non-file/reparse rejection and
handoff errors. Reuse existing fixtures and opening/search/hotkey regressions.
Run Windows Debug/Release build/test/format/lint presets and targets. Inspect
native Explorer selection with an inert synthetic file and preserve real user
windows/data. README and feature status reflect actual verification. Windows
combined ASan/UBSan remains unsupported; no new Linux work or sanitizer claim.

## References

- https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shopenfolderandselectitems
- https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shparsedisplayname
