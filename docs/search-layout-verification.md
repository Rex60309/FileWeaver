# Windows search layout verification

2026-11-12. User-requested Phase 2 layout/filter/orientation corrections.
No source-file writes, schema/dependencies/network or activation policy changes.
See ADR-0040 for the shared C++ filter contract and thumbnail limits.

## Regression evidence

Generated JPEG with EXIF orientation 6 failed before correction: actual height
128, expected width*2=512. Applying reader auto-transform and preserving the
transformed aspect ratio fixes this case. Added mirrored orientation fixture,
image/text visibility and fitted-size checks, sidebar union/clear/limit checks,
and shared storage tests for empty/multiple/duplicate/no-extension values,
intersection with legacy ext, parameter binding, malformed input and >64 values.

## Commands and results

Verification in progress. Presets use installed pinned dependencies. Windows locks
old test images; ignored build/local-test-output.cmake renames only phase2_tests
output to phase2_tests_layout_reviewed via CMAKE_PROJECT_FileWeaver_INCLUDE, without
changing test contents or coverage. Hosted builds keep ordinary filenames.

The Release application image was locked even after the user closed its window;
Windows listed a process record with zero threads and no window. No force-kill,
permission changes or reboot was attempted. The ignored local hook additionally
sets Release fileweaver-desktop OUTPUT_NAME to fileweaver-desktop-updated, reusing
the same build/dependencies. Full Release build then passed; ordinary clean/CI
output naming is unchanged. The new executable includes all prior features.

Commands (TEMP/TMP point at build/test-temp):

```powershell
cmake --preset desktop-debug "-DCMAKE_PROJECT_FileWeaver_INCLUDE=$PWD/build/local-test-output.cmake"
cmake --build --preset desktop-debug --parallel 4
cmake --preset desktop-release "-DCMAKE_PROJECT_FileWeaver_INCLUDE=$PWD/build/local-test-output.cmake"
cmake --build --preset desktop-release --parallel 4
cmake --preset debug
cmake --build --preset debug --parallel 4
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset debug --output-on-failure
ctest --preset release --output-on-failure
ctest --preset desktop-debug --output-on-failure
ctest --preset desktop-release --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
```

Focused regressions passed: phase2_tests_layout_reviewed.exe with
jpegOrientationIsAppliedWithoutDistortion, jpegMirrorOrientationIsApplied,
imagePreviewUsesPaneWithoutEmptyTextEditor, sidebarTypesLimitAndCompactLayout
(6 including init/cleanup); storage_tests.exe '*Multi-extension*' passed 8
assertions. Synthetic fixtures only. Logs remain ignored under build/layout-*.
Full suite and static analysis results are pending.

## Full regression checkpoint

Core Debug/Release: 13/13 each, logs layout-tests-debug.log and
layout-tests-release.log. Final desktop Debug/Release: 16/16 each, logs
layout-tests-desktop-debug-reviewed.log (73.85s) and
layout-tests-desktop-release-reviewed.log (73.61s). Core format/lint passed.
Desktop format passed; final desktop lint remains pending at this checkpoint.
No tests skipped; no timeout/assertion/warning suppression changes.

The first full desktop run exposed the old PDF UI test's fixed (128,128) sample
coordinate falling outside the now-resized display pixmap. It now samples the
actual display center and still verifies red first-page content, then blue
content after replacement, opt-in and hide/cache lifecycle. Decoder-size/resource
assertions remain unchanged in parser tests. Initial lint reported a potential
Qt layout ownership leak; unique_ptr with explicit setLayout ownership transfer
replaces the new raw layout allocation. A later lint pass was deliberately
stopped to correct visual QA findings before the final run; it is not a pass.

Private visual QA used onboarding_tests.exe
confirmedCreationBrowsesAndReopeningDoesNotRescan with
FILEWEAVER_ONBOARDING_CAPTURE=build/layout-actual-window-reviewed.png. Qt minimal
with fonts and Fusion style renders actual widgets using generated temporary
files. Visual review found the type group expanded its internal spacing when
preview was off; fixed vertical sizing plus remaining-space allocation corrected
it. The new group-height regression verifies the compact group. The resulting
screenshot is not a native Windows theme capture and stays ignored under build.
Default window is now 1280x900 bounded by available screen size. User can collapse
advanced controls and adjust the splitter for more visible files.

No current hosted MSVC/sanitizer run is claimed. Supported portable-core sanitizer
CI remains separate; local combined Windows GUI ASan/UBSan is unsupported. No
formal performance measurement, unlimited browsing or original-resolution image
preview claim is made. Current source has not been pushed or released.

## Final local acceptance

Final desktop lint exited 0, log build/layout-lint-reviewed.log, with no new
warning suppressions; desktop format exited 0, build/layout-format-reviewed.log.
Core format/lint exited 0, build/layout-core-format.log and layout-core-lint.log.
Final reviewed desktop builds both exited 0. Full core Debug/Release 13/13 and
reviewed desktop Debug/Release 16/16 all passed. Final git diff --check passed.
Feature status is Tested locally; no hosted run or release readiness is claimed.
The preceding pending checkpoints are historical observations, superseded here.

Safety: tests used generated temporary files only; user source files and existing
indexes were not scanned or modified. No dependency/schema/runtime-network change.
The shared read-only search API gained the bounded extension-set field described
in ADR-0040; CLI option/JSON contracts remain unchanged. GUI categories/compact
state are transient. Existing preview opt-in, subprocess/IPC/cache caps remain.
Remaining: hosted MSVC and supported-core sanitizer verification; no pagination,
unlimited results or higher-resolution decoding has been implemented.

## Follow-up: horizontal filter toolbar and preview notice

2026-11-12: user requested removing the generic successful-preview notice and
moving file types into one horizontal row. Type checkboxes and the existing
1-1000 result control now sit above the list/preview splitter. The right pane is
hidden completely when preview is disabled. With preview enabled its entire
area is reserved for content, loading/errors and necessary format/truncation
messages. Normal text/image success has no generic notice and hides its label.
The read-only/opt-in and parser-resource policies are unchanged.

Existing desktop regressions now assert horizontal checkbox alignment, compact
toolbar height and a hidden notice for a successful PNG preview. Real-widget
visual inspection used generated temporary files, Qt minimal with fonts/Fusion,
and build/toolbar-actual-window.png; this is not a native-theme capture. The
visual test passed 3/3 including setup/cleanup. Output and logs remain ignored.

Same exact preset commands as above; ignored phase2_tests output name is now
phase2_tests_toolbar. Application remains fileweaver-desktop-updated.exe locally.
Logs for this follow-up use build/toolbar-*. Final acceptance is pending.
No source files/indexes, schema, dependencies, clipboard or runtime network
behavior were changed; no higher-resolution preview or new formats were added.

### Follow-up final local acceptance

Core Debug/Release 13/13 each; desktop Debug/Release 16/16 each, no skipped tests.
All core/desktop builds, format-check and lint completed with exit 0. Logs:
build/toolbar-tests-{debug,release,desktop-debug,desktop-release}.log,
toolbar-{debug-build-final,release-build,format,lint}.log and
toolbar-core-{debug-build,release-build,format,lint}.log. git diff --check passed.
The pending follow-up checkpoint above is superseded by these results. No current
hosted MSVC/portable-core sanitizer execution is claimed; local Windows GUI
combined ASan/UBSan remains unsupported. No release or push is part of this slice.
