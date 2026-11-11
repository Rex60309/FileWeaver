# Windows Phase 2 completion ledger

Date: 2026-11-11 (Asia/Taipei). User explicitly authorized completing Phase 2 and
confirmed the scope below. PRODUCT_SPEC sections 4-6, 20, 23 and Phase 2 deliverables
apply. Windows remains primary; no release package or Phase 3-5 functionality.

| Requirement | Current evidence / remaining implementation |
| --- | --- |
| Qt GUI, bounded async search, stale-result cancellation/navigation | Existing tested baseline |
| Global hotkey, collision/configuration/focus | Existing baseline; add Escape hide while enabled, X quits |
| Fuzzy matching/configurable deterministic ranking | Existing baseline; add path-component matching |
| Glob and literal path modes | Implemented core + CLI + GUI; local generated E2E passed, hosted pending |
| Visual filters/sort matching CLI | Implemented; local GUI request mapping passed, hosted pending |
| Real-time watching/reconciliation | Single opt-in root baseline, successful hosted run 42774849289 |
| Safe first-batch previews | Implemented TXT/Markdown/JSON/XML/CSV/source code/PNG/JPEG, syntax highlighting and bounded thumbnail cache; generated helper/Qt tests passed, hosted pending |
| Untrusted parser limits/cancellation | Isolated helper process, Windows job memory/CPU/wall limits, bounded IPC, no execution of document data |
| Optional settings persistence | Non-sensitive weights/hotkey combination/font preferences only; preview/watch/hotkey activation never restored |
| Verification/docs | Generated regression/E2E/Qt fixtures, required Windows gates, hosted CI, README/status updated per slice |

User approved enabling pinned Qt PNG/JPEG features and associated downloads, explicit
preview opt-in/content reads, isolated helper, optional preference storage and the
Escape behavior change. Existing opening allowlist and per-operation confirmation
stay. PDF/GIF are the second preview batch in section 6.1 and are not part of this
accepted completion scope. Directory/application/project result categories belong
to later authorized capabilities; do not add executable/script launching or Phase 4
projects. Multi-root/CLI watch, broader Missing reconciliation, content indexes and
release packaging remain separate work. This ledger is not a completion claim.

## Local verification in progress

Windows 11, Intel i7-13700KF, 34,104,291,328 bytes RAM; GCC 13.2.0,
CMake 3.29.2, pinned Qt 6.11.3. Debug and Release core CTest: 13/13;
desktop Debug and Release CTest: 15/15. Additional preview-window opt-in test
and explicit integer/API corrections were subsequently added and focused tests
passed; final format/lint and post-change checks remain in progress.

Commands executed:

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel
ctest --preset desktop-debug --output-on-failure
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel
ctest --preset desktop-release --output-on-failure
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
```

CTest used generated fixtures under build/test-temp; junction/hard-link fixture
creation required unrestricted test execution. Initial restricted runs failed
those fixture operations, not product assertions. The old unsupported-mode test
used `glob`, now authorized and valid; it now tests `unsupported`, with new valid
and invalid glob coverage retained. New fixture failures (UTF-16 byte typo and
unequal PNG compressed sizes) were corrected; equal-size/equal-mtime PNG
replacement still verifies checksum-based cache invalidation.

Native Windows observation: opened actual Debug window, confirmed visible
controls and first-launch disabled hotkey/preview/watch/preference persistence,
opened visual filter/sort dialog, closed dialog and application. No actual source
root, content or user clipboard used. Content rendering and source-byte retention
are covered by generated helper/Qt interaction fixtures, not claimed native user
content observations.

Regression benchmark command:

```powershell
python benchmarks/search/fuzzy_search.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --output build/phase2-fuzzy-baseline.json --rows 100000 --repetitions 5
```

[Raw report](benchmarks/phase2-fuzzy-windows-100k.json): 100,000 synthetic metadata
rows, four queries, first uncontrolled run plus five warm repetitions; every
result checked. CLI process timing includes startup/JSON/memory observer. Storage
medium unmeasured; no controlled cold-cache or SPEC latency-target claim.
Windows combined ASan/UBSan unsupported for this configured compiler; hosted
portable-core sanitizer coverage remains to be executed for this revision.

## Fresh local build after a Windows executable lock

Final UI help strings and explicit include cleanup triggered relinking. Windows
refused to overwrite phase2_tests.exe in both existing build directories. Read-only
Win32_Process inspection found two already-terminated records (zero handles and
zero threads); termination was denied by Windows. No security setting or test was
changed. A write-handle diagnostic was rejected by automatic review as potentially
overwriting the executable; only read-only diagnostics were used afterward.

Fresh directories reused the same pinned installed dependency tree (no new
package download) and the repository desktop presets:

```powershell
$dependencyOptions = @(
  '-DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe',
  '-DCMAKE_MAKE_PROGRAM=C:/Strawberry/c/bin/ninja.exe',
  "-DCMAKE_TOOLCHAIN_FILE=$PWD/build/tools/vcpkg/scripts/buildsystems/vcpkg.cmake",
  '-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic',
  '-DVCPKG_HOST_TRIPLET=x64-mingw-dynamic',
  "-DVCPKG_INSTALLED_DIR=$PWD/build/desktop-dependencies",
  '-DVCPKG_MANIFEST_INSTALL=OFF'
)
cmake --preset desktop-debug -B build/phase2-clean-debug @dependencyOptions
cmake --build build/phase2-clean-debug --parallel
ctest --test-dir build/phase2-clean-debug --output-on-failure
cmake --preset desktop-release -B build/phase2-clean-release @dependencyOptions
cmake --build build/phase2-clean-release --parallel
ctest --test-dir build/phase2-clean-release --output-on-failure
```

Both fresh builds passed; both full CTest runs passed **15/15**, including generated
helper image/text/protocol/timeout/cancellation, GUI opt-in, cache invalidation,
preferences, hotkey and existing watcher/open/reveal interaction tests. Existing
primary application executables linked successfully; the lock affects overwriting
the old generated test executables. A machine/session restart may be needed to
clear those old OS references. This is a fresh build-directory check, not a claim
of a clean Git checkout or a packaged release.

## Font preference regression

A focused generated Qt regression failed before the fix: the minimal-platform
window inherited point size 12 while the visible preference was 10. The constructor
now applies the declared default to both interface and fixed-font preview before
loading any saved preferences. The same focused test passed after the fix:

```powershell
build/phase2-clean-debug/phase2_tests.exe defaultFontMatchesDisplayedPreference -o build/font-regression-before.txt,txt
cmake --build build/phase2-clean-debug --parallel
build/phase2-clean-debug/phase2_tests.exe defaultFontMatchesDisplayedPreference -o build/font-regression-after.txt,txt
```

Before: 2 passed / 1 failed; after: 3 passed / 0 failed. Full suite and final static
analysis are repeated after this change. No source files or preference activation
states are changed by this correction.

## Final local gate results before hosted verification

After the font correction and CLI help clarification:

- Core Debug / Release builds and complete CTest: **13/13 each**.
- Latest Debug desktop full CTest in build/phase2-clean-debug: **15/15**.
- Latest Release desktop fresh build/full CTest in build/phase2-final-release:
  **15/15** (same dependencyOptions/preset as the preceding fresh Release check).
- `cmake --build --preset debug --target format-check` and `--target lint`: passed.
- `cmake --build --preset desktop-debug --target format-check` and `--target lint`:
  passed, with project diagnostics treated as errors; external/generated warnings
  remain reported separately by the existing configuration.
- `cmake --build --preset desktop-debug --target fileweaver-desktop --parallel`
  and the corresponding desktop-release application target: passed, updating the
  usual application locations despite old generated-test executable locks.
- `git diff --check`: passed. No generated binaries, real data or runtime databases
  are included in the change.

The final Release configure/build/test commands use `build/phase2-final-release`
in place of `build/phase2-clean-release`, whose previously tested executable also
remained locked by a terminated Windows process record. No test was skipped or
replaced with stale results. Hosted MSVC and portable-core sanitizer verification
are pending for the committed source revision; Phase 2 is not yet declared accepted.
