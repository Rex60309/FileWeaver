# Windows global-hotkey verification

Date: 2026-11-09. Phase 2 scope: opt-in hotkey activation of the existing Windows
search window; not completion of the launcher or Phase 2. Release packaging was
explicitly excluded by the user. ADR-0028 records the selected design and default.

## Changes and safety

apps/desktop/global_hotkey.* owns one GUI-thread RegisterHotKey registration,
with a typed injectable adapter and explicit cleanup. search_window.* provides
opt-in, editable key sequence, separate plain-text status, and restore/focus.
CMake links the existing Windows user32 system library only in the desktop target.
No new third-party library, worker, schema, migration, file launch, user-file
mutation, clipboard collection, persistence, autorun or product network behavior.
Core and CLI contracts remain unchanged. Query text is retained and selected on
activation; no source root is scanned implicitly.

Default Ctrl+Alt+Space was explicitly selected by the user. Registration is off
at each start. Users must disable before editing the chord; failures do not take
another application's registration. Failed cleanup remains visible and can be
retried; closing the process ends its thread-bound registration. OS foreground
policy can restrict activation. No hook or synthetic input bypass is used by the
product. The recording editor receives keys while editing; Escape closes the
window from the search/results area, rather than overriding editor recording.

## Regression and interaction evidence

The initial mock-only tests passed, but native inspection found that the actual
thread WM_HOTKEY arrived through Qt's windows_generic_MSG path. A new regression
posts a synthetic message to its own GUI thread using PostThreadMessageW, with
fake OS registration. It failed with activated.count() == 0 before the fix, then
passed after accepting both Windows event tags and validating null HWND, ID,
key and modifiers. No skipped/quarantined tests or disabled checks were added.

Qt tests force the minimal platform and fake registration adapters; they do not
register real global hotkeys or read the native clipboard. Existing generated
Unicode/offline index fixtures and async-search tests are retained. The suite
also checks invalid/reserved/multi-chord input, constructor validation, mapping,
MOD_NOREPEAT, collision retry, cleanup failure retention/retry, destructor cleanup,
wrong event tags/IDs/modifiers, minimized restore, focus and selected Unicode query.

Native Windows inspection used two separately launched FileWeaver processes and
synthetic query text only. Observed:

- Default registration off; manual Ctrl+Alt+Space enable succeeds.
- A second process reports the occupied combination and stays disabled.
- With the first process minimized and the second foreground, sending the actual
  combination restores the first and selects its complete synthetic query.
- Closing the first permits the second to register the same combination.
- Disabling allows editing; Ctrl+Shift+F10 can be recorded and enabled.
- Both test processes were closed after inspection, releasing their registrations.

This verifies the tested Windows setup, not all keyboard layouts, accessibility,
DPI configurations, OS versions or foreground-policy conditions.

## Executed local gates

Windows GCC 13.2 x64, Qt 6.11.3, C++20, CMake 3.29.2, Ninja and the repository's
pinned vcpkg dependencies. Tests use owned build/test-temp via TEMP/TMP; existing
junction/hard-link fixtures require execution permissions to create those links.

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel
ctest --preset desktop-debug --output-on-failure
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel
ctest --preset desktop-release --output-on-failure
```

All configure/build/format/lint commands above passed. Core Debug/Release: 11/11
CTest tests passed each. Desktop after the dispatch fix: Debug/Release 12/12
passed each; Qt reports 11 passes (nine test functions plus init/cleanup), zero
failures/skipped/blacklisted. Initial lint diagnostics were corrected using typed
binding fields, unsigned masks and the supported Qt exception assertion; checks
were not globally suppressed. clang-format 23.1.5 and clang-tidy 23.1.4 were used.

The supported Linux core sanitizer rerun passed 11/11 in 213.30 seconds. Exact
commands in the existing Ubuntu environment:

```sh
cmake --preset sanitizers -G "Unix Makefiles" -B build/linux/sanitizers -DCMAKE_PREFIX_PATH="$PWD/build/linux/dependencies"
cmake --build build/linux/sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/linux/sanitizers --output-on-failure
```

Windows combined ASan/UBSan remains unsupported by the configured preset; no
sanitizer coverage of the Windows-only hotkey adapter is claimed. No performance
benchmark result is claimed. Hosted CI for this implementation revision passed; see the evidence below.

## Remaining scope

Settings persistence, tray/autorun behavior, file opening, fuzzy ranking,
watchers and previews are not part of this slice. No release ZIP/installer is
created. The next proposed smallest milestone is explicitly selected file opening
with error handling; it requires a separate scope decision before implementation.

## Hosted Windows acceptance

Verified implementation: `4b0ad67b249a8abaa21c87282aefa2ad72650fc5`.
[Successful quality run 42611933517](https://github.com/Rex60309/FileWeaver/actions/runs/42611933517)
completed on 2026-11-09. Windows/MSVC 19.44.35229 desktop Debug and Release each
passed 12/12 CTest tests, including the queued-thread-message hotkey regression.
Observed desktop test times: Debug 70.88 seconds; Release 47.14 seconds.
Windows/MSVC core Debug/Release each passed 11/11. All four workflow jobs passed;
the already completed Linux/core-analysis evidence is retained above. No extra
Linux work was scheduled after the user requested Windows-only prioritization.
Future implementation and acceptance focus on Windows unless the user changes
that scope. Release packaging remains excluded.

This follow-up changes Markdown documentation only and uses `[skip ci]` to avoid
rebuilding unchanged Qt dependencies. Application source, workflow, dependencies
and tests are identical to the fully verified implementation revision above.
No failing test was bypassed. git diff --check passed for the documentation update.
