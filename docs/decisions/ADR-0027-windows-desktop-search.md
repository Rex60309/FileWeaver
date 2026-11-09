# ADR-0027: Windows desktop search milestone

Date: 2026-11-09
Status: Accepted; user explicitly approved this Phase 2 scope, Qt download and worker model

## Scope and alternatives

Qt Widgets is selected over native Win32 UI or Qt Quick to follow the specified
stack with a small conventional desktop surface. This slice does not attempt the
entire Phase 2 roadmap.

Smallest Phase 2 slice: opt-in Qt 6 Widgets application for Windows, opening an
explicitly selected existing index and searching it through shared search_files.
Support the shipped exact/substring/prefix modes, keyboard result navigation,
copying a selected path on explicit user action, and visible errors/empty states.
Do not scan sources implicitly or change the schema/CLI contract.

Qt desktop build is disabled by default and dependencies are opt-in through a
vcpkg feature and CMake option, preserving the existing core build. No model,
telemetry, clipboard collection, content preview, watcher or source-file mutation.
Hotkey registration, file launching and fuzzy ranking are separate milestones.

## Execution model and safety

Debounce typed input. One owned search worker at a time, one latest pending request,
stop-token cancellation and generation-based stale-result rejection. Worker owns
its SQLite connection via the existing use case. Only the UI thread changes
widgets. Cancel and join on shutdown. No thread per keystroke/file and no SQL in
UI callbacks. Startup/database selection does not open source files.

## Acceptance and verification

Build Debug/Release desktop executables on Windows; test normal/invalid database,
Unicode results, keyboard navigation, replacement of stale results and shutdown.
Use only generated temporary fixtures. Verify default CLI-only configuration and
all Phase 1 regression gates remain passing. Run Qt interaction tests plus native
Windows visual inspection before claiming the interface is usable. Update README
and feature-status only to the behavior actually verified.

## Authorization

The user explicitly selected this Phase 2 scope and approved the Qt Widgets
dependency/download and bounded worker model before implementation, as required
by AGENTS.md sections 1 and 2.6.

The completion side also stages at most one reply and one queued GUI wake-up,
preventing an unbounded event queue. Stored names/paths must decode as strict
UTF-8 before rendering or copying. Errors are plain text; long visible previews
are bounded while explicit copy retains the complete selected path. The view
does not create a filesystem model, stat result files, fetch icons or interpret
metadata as HTML. Native clipboard ownership is checked after explicit copying;
clipboard contents/history are never collected. Qt tests force the minimal
platform so their synthetic clipboard assertions cannot read the user's clipboard.
The selected minimal Qt module set does not build the freetype-dependent offscreen
plugin; minimal uses Qt's process-local fallback clipboard. Native Windows UI is
inspected separately with the windows platform and modern Windows style plugin.

Shutdown cancels and joins the owned worker. An underlying blocking OS/SQLite
operation can delay shutdown; no forced thread termination or hard cancellation
latency guarantee is claimed.

Qt layouts/items transfer ownership directly to their parent/model using the
returned unique_ptr::release pointer. The lint header exclusion is limited to
Qt-generated *_autogen directories; handwritten application and test headers
remain analyzed with the existing checks and warnings-as-errors policy.

CI dependency preparation normalizes the PCRE2 gitlink API response formatting
but requires its original pinned SHA512 before writing cache; it does not alter
package versions, hashes, dependency scope or product network behavior. The
Windows developer environment must retain the explicitly pinned vcpkg root.
