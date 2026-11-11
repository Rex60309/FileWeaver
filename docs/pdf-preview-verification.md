# Windows PDF first-page preview verification

Date: 2026-11-12 (Asia/Taipei). Status: local implementation and gates passed; hosted validation pending,
no final PDF acceptance or release claim. The user authorized pinned Poppler, dependency
downloads and a static first-page preview. ADR-0037 records the design and limits.

## Scope

Manual opt-in, per-launch off, Windows only. Read at most 8 MiB and render only
page zero to at most 256x256 RGBA pixels in the existing isolated helper. Keep
192 MiB private-memory, 2 s user-CPU, 5 s wall and 512 KiB IPC bounds. Reject
encrypted/locked PDFs; no password collection, document scripts, form interaction,
attachment access or link activation. No source mutation or schema migration.

The GUI does not link the parser; only the renderer/helper and trusted-fixture
tests link Poppler. Diagnostic text is discarded, with generic errors and a
bounded count. Installed font lookup is possible; resource isolation is not an
OS permissions sandbox. First-page labels survive checksum-validated cache hits.

## Dependency evidence

Official Poppler 26.11.0 archive, fixed SHA-512 in the local overlay; the global
vcpkg baseline and Qt version remain unchanged. The overlay disables unused
frontends, utilities, curl, signing, font subsetting and optional Brotli streams.
Poppler's own implementation uses C++23; FileWeaver targets and its adapter stay
C++20. Public-header compatibility requires actual builds, not an assumption.
GPL-2.0-or-later and transitive obligations need review before future distribution.

Dependency downloads are development-time only. One interrupted MSYS2 Perl
download was retrieved from MSYS2's listed dotsrc mirror and validated against
the exact vcpkg SHA-512 before entering the cache; no hash check was bypassed.

## Verification pending

Generated fixtures cover multipage colors, Unicode, unchanged source, cache
invalidation, untrusted JavaScript action data, a locked security dictionary,
corruption, empty and oversized files, page geometry, cancellation and protocol
flag validation. Existing preview process limits, stale work, GUI opt-in and
non-PDF regressions remain mandatory. Record exact commands and observed results
here after execution, including local and hosted compiler coverage.


## Local commands and observed results

Windows 11, GCC 13.2.0, Qt 6.11.3, CMake 3.29.2. Core Debug/Release CTest
passed 13/13 each; desktop Debug/Release passed 15/15 each. Both desktop reports
include `pdfFirstPageLimitsCacheAndUntrustedActions`: Phase2Tests 14 passed,
0 failed/skipped; desktop interaction 23 passed, 0 failed/skipped. Core format
and lint passed; desktop format passed, desktop lint initially rejected a split literal in the generated security dictionary
(`bugprone-suspicious-missing-comma`). It was replaced with explicit QByteArray
construction without suppressions; both rebuilt desktop suites again passed 15/15. The full lint rerun passed with no project warnings.

Dependency installation completed successfully (including Poppler Debug/Release):

```powershell
$env:VCPKG_ROOT=Join-Path $PWD 'build/tools/vcpkg'
$env:PATH='C:\Strawberry\c\bin;'+$env:PATH
$env:VCPKG_MAX_CONCURRENCY='4'
& build/tools/vcpkg/vcpkg.exe install --triplet x64-mingw-dynamic --host-triplet x64-mingw-dynamic --x-feature=desktop --x-install-root=build/desktop-dependencies --allow-unsupported
```

The configured community MinGW triplet requires the explicit allow-unsupported
switch; it does not disable hash verification or project warnings/tests.
As in the prior Phase 2/GIF ledger, old generated test executables remain locked
by terminated Windows process records. Fresh preset-derived directories avoid
relinking those binaries; no tests are skipped and no system settings changed.

```powershell
$options=@(
  '-DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe',
  '-DCMAKE_MAKE_PROGRAM=C:/Strawberry/c/bin/ninja.exe',
  "-DCMAKE_TOOLCHAIN_FILE=$PWD/build/tools/vcpkg/scripts/buildsystems/vcpkg.cmake",
  '-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic',
  '-DVCPKG_HOST_TRIPLET=x64-mingw-dynamic',
  "-DVCPKG_INSTALLED_DIR=$PWD/build/desktop-dependencies",
  '-DVCPKG_MANIFEST_INSTALL=OFF'
)
$env:TEMP=Join-Path $PWD 'build/test-temp'
$env:TMP=$env:TEMP
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
cmake --preset desktop-debug -B build/pdf-debug @options
cmake --build build/pdf-debug --parallel 4
ctest --test-dir build/pdf-debug --output-on-failure
cmake --build build/pdf-debug --target format-check
cmake --build build/pdf-debug --target lint
cmake --preset desktop-release -B build/pdf-release @options
cmake --build build/pdf-release --parallel 4
ctest --test-dir build/pdf-release --output-on-failure
```

Tests use generated temporary fixtures only. An initial direct GUI-subsystem test
launch returned no console report and is not counted as acceptance evidence; the
CTest wrappers above require actual Qt reports and zero failures/skips.
Hosted MSVC and portable-core sanitizer results are pending. Combined ASan/UBSan
is not supported by this local Windows/GCC setup; do not infer PDF/Qt sanitizer
coverage from the existing portable-core analysis job. No performance claim.


The standard desktop application/helper targets were also refreshed successfully:

```powershell
cmake --preset desktop-debug
cmake --build --preset desktop-debug --target fileweaver-desktop --parallel 4
cmake --preset desktop-release
cmake --build --preset desktop-release --target fileweaver-desktop --parallel 4
```
