# ADR-0029: Confirmed opening of supported Windows local files

Date: 2026-11-09
Status: Accepted; user explicitly approved common document/image/media types,
full-path confirmation per operation, and exclusion of programs/scripts/shortcuts.

## Context and alternatives

Continue Phase 2 on Windows only; release packaging and additional Linux work
are excluded. Reuse indexed SearchResult paths and the existing Qt window.
Opening selection implicitly would change the read-only search safety boundary.
The chosen action is an immutable selected-path snapshot, shown fully in a
scrollable plain-text confirmation dialog with Cancel as the default. Only an
explicit Open action hands that snapshot to the background operation. Selection,
search, cancellation and rejection never launch anything. Button, result Enter
and result double-click lead to the same confirmation; query Enter still searches.

## Decision and consequences

A desktop policy allowlists txt/md/pdf/docx/xlsx/pptx/csv, png/jpg/jpeg/bmp/gif/webp,
mp3/wav/flac/aac/m4a/ogg and mp4/mkv/avi/mov/webm. Everything else is rejected,
including executable, script, shortcut, macro-enabled and archive extensions.
This is a filename policy, not content inspection, malware detection or a sandbox.
An external viewer may process active content, write its own files or access the
network according to its own behavior. FileWeaver does not modify source bytes.

Require a well-formed local drive-qualified native path with no URL/UNC/device
namespace, alternate stream, ambiguous dot/space component or direction-control
characters. Preserve ordinary Unicode and spaces. Before launch, inspect the
current path components in order using guarded Win32 metadata APIs, rejecting
missing/non-file targets, remote drives, offline attributes and all reparse points
(including ancestor junctions). Do not read contents or initialize/update an index.
No on-disk schema, privacy collection, new third-party dependency or CLI change.

Use a narrow injectable FileOpenApi and one dedicated persistent worker, separate
from the search worker. At most one confirmed request, no queued retries; GUI work
remains responsive. Initialize COM STA on that worker and call ShellExecuteExW
with the explicit open verb, the complete Unicode file path and no argument/shell
string. Use a Windows system working directory, keep OS security prompts enabled,
and report errors without Shell error dialogs. Shell acceptance is a handoff,
not proof the associated app displayed the file successfully. No automatic launch
of an executable, a script, a discovered project command or a chosen handler.

Cancellation is checked before inspection and immediately before handoff. Once
the shell operation begins it cannot be recalled. OS/COM/handler calls can block;
shutdown joins the worker and has no hard deadline. Filesystem changes can still
race between metadata validation and path-based shell opening; no atomic identity
binding or adversarial replacement protection is promised. Locking user files
through handoff was rejected because it can prevent normal associated apps from
opening for editing and still cannot control external app behavior.

## Acceptance

Generated temporary fixtures only. Test Unicode paths, policy exclusions,
missing/replaced targets and junctions, cancelled confirmation, immutable path
snapshot, launcher failure, cancellation before handoff, one-operation backpressure,
GUI responsiveness, safe default and keyboard controls. Fake launcher adapters in
automated tests must never start external user applications. Separately inspect
the native confirmation/interaction with a synthetic file. Run Windows Debug and
Release build/test/format/lint gates; no additional Linux work. Windows combined
ASan/UBSan is unsupported, which must remain an explicit coverage limitation.
README and feature status stay In Progress until acceptance evidence exists.

## References

- https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw
- https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shellexecuteinfow
