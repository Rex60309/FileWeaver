# ADR-0028: Opt-in Windows global hotkey

Date: 2026-11-09
Status: Accepted scope; user continued the proposed Windows hotkey milestone.

## Context and alternatives

The prior verified search window has no system-wide activation. The user asked
to continue implementation and explicitly excluded release packaging. This slice
adds configurable, explicitly enabled activation of the existing search window,
not the entire launcher described in PRODUCT_SPEC section 5.

Use Windows RegisterHotKey through a narrow injectable desktop adapter, and Qt's
native event filter for WM_HOTKEY. A low-level keyboard hook is unnecessary and
would observe unrelated keystrokes. No new library, thread, IPC, database schema
or persistent settings are added. Keep one registration on the GUI thread.

## Decision and consequences

Default registration is off. Propose Ctrl+Alt+Space rather than the optional
Alt+Space example (which overlaps the Windows system menu); users may choose
another supported combination. The user explicitly selected Ctrl+Alt+Space in the preference reply. It
registers nothing before checkbox opt-in.
Allow one chord with Ctrl or Alt, optional Shift, and an ASCII letter/digit,
Space or F1-F24 except reserved F12. Reject Windows-key and multi-chord sequences.
Enable MOD_NOREPEAT. Report registration errors visibly without replacing any
other application's hotkey. Keep the editor disabled while registered: disable,
edit, then enable. Failed unregistration retains owned state and supports retry;
destruction retries cleanup and process/thread exit ends the native registration.

Activation restores the existing window (preserving maximized state), requests
foreground focus and selects the query. Windows foreground policy can still
restrict activation; do not use input attachment, synthetic input or hooks to
bypass OS focus policy. No tray/autorun/background service, persistent preferences,
file opening, file mutation, watcher or clipboard collection. Escape still closes
the app; the hotkey works only while this process is running and enabled.

## Acceptance

Test opt-in default, mapping/validation, conflict and retry, failed cleanup,
explicit disable/destructor cleanup, unrelated/stale WM_HOTKEY rejection,
restoration/focus and retained search behavior with fake OS calls. Qt minimal
interaction tests must not register real global keys. Separately inspect native
Windows registration and activation using synthetic queries only. Run the
existing desktop/core build, test, format, lint and supported sanitizer gates.
README and feature status distinguish tested behavior from remaining Phase 2.

## References

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unregisterhotkey
- https://doc.qt.io/qt-6/qabstractnativeeventfilter.html

## Implementation evidence and correction

The registration request uses a named HotkeyBinding value to avoid adjacent
interchangeable integer arguments. The native adapter captures GetLastError
immediately and retains an explicit failure even if the OS error code is zero.

Native inspection exposed a dispatch mismatch missed by the initial direct-call
mock test. In the pinned Qt 6.11.3 source, qeventdispatcher_win.cpp forwards queued
thread messages through windows_generic_MSG; windows_dispatcher_MSG is used by
the internal window procedure. Accept both event tags while requiring WM_HOTKEY,
a null HWND (our registration is thread-bound), owned ID and matching key/modifiers.
A PostThreadMessageW regression posts only to the test's own GUI thread with a
fake registration adapter: it failed before correction and passed afterward.
No real global key, external input, hook or user data is used by this regression.
Native two-process inspection then confirmed minimized-window restoration and
query selection from a real registered key combination.
