# Windows Phase 2 preview follow-up verification

Date: 2026-11-12 (Asia/Taipei).
Status: all local gates passed; hosted MSVC/CI acceptance pending.

## Authorized scope

Continue Windows Phase 2; Linux deferred. Complete PDF integration acceptance and
the explicitly approved BMP static-preview slice using existing Qt, with no new
package or schema. ADR-0038 records the format decision and limits. Source files
remain read-only; all fixtures are generated in temporary directories.

## Behavior and regression evidence

- Recognize .bmp case-insensitively and explicitly select the built-in Qt BMP
  handler. Reuse 8 MiB input, 8192 maximum dimensions, 16,777,216 pixel cap,
  <=256x256 output, checksum-validated memory cache and existing helper limits.
- Normalize a bounded negative BMP height for top-down storage before the common
  dimension check. Never negate unbounded negative dimensions or INT_MIN.
- Handle QWidget hideEvent to disable preview and cancel/clear its worker/cache.
  Reopening requires explicit opt-in and reads current source bytes.

The BMP test first failed with Unsupported preview type. After enabling the
handler, the generated valid top-down case failed with the common dimension
error; bounded height normalization fixed it. The GUI PDF lifecycle test failed
because direct hide left the preview checkbox enabled; hideEvent fixed that
documented behavior. These are real before/after test results, not inferred passes.

Focused Qt report: 5 passed, 0 failed/skipped/blacklisted (including init/cleanup):
bmpStaticPreviewLimitsAndCache, pdfAspectRatioAndGeometryBounds and
pdfWindowOptInAndVisibilityLifecycle. BMP covers Unicode/uppercase extension,
red/blue pixels, aspect ratio, top-down, unchanged source, cache hit/invalidation,
dimension/pixel caps, extreme signed heights, truncation, empty/oversized input and
cancellation. PDF additionally covers white padding, page-geometry boundary,
tiny-page rejection, visible first-page labeling, opt-in, hide clearing, renewed
content and manual clear.

## Exact verification commands

Reuse the preset-derived build/pdf-debug and build/pdf-release directories and
pinned installed dependencies from pdf-preview-verification.md. Standard generated
test binaries have the previously recorded Windows stale-process locking issue;
fresh directories run the complete suites without skipping tests.

```powershell
$env:TEMP=Join-Path $PWD 'build/test-temp'
$env:TMP=$env:TEMP
cmake --build build/pdf-debug --parallel 4
$testRun=Start-Process -FilePath "$PWD/build/pdf-debug/phase2_tests.exe" -ArgumentList @(
  'bmpStaticPreviewLimitsAndCache','pdfAspectRatioAndGeometryBounds',
  'pdfWindowOptInAndVisibilityLifecycle','-o',
  "$PWD/build/windows-preview-followup-focused.txt,txt"
) -Wait -PassThru -WindowStyle Hidden
Get-Content build/windows-preview-followup-focused.txt
# Inspect $testRun.ExitCode (observed 0), not just the process launch result.
cmake --build build/pdf-release --parallel 4
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
ctest --test-dir build/pdf-debug --output-on-failure
cmake --build build/pdf-debug --target format-check
cmake --build build/pdf-debug --target lint
ctest --test-dir build/pdf-release --output-on-failure
```

Full-suite/gate results are recorded below. No performance claim.
Local combined ASan/UBSan is unsupported on this Windows toolchain; existing
portable-core CI coverage must not be represented as PDF/Qt sanitizer coverage.
The prior PDF CI run 43096237771 was still queued on resumption; a queued job is
neither a test pass nor a failure. No user-file mutation, schema migration, runtime network or persistent
content-storage changes are introduced by this follow-up.


## Observed local results

Windows 11, GCC 13.2.0, Qt 6.11.3, CMake 3.29.2. Desktop Debug and Release
full CTest each passed 15/15. The Phase2Tests wrapper now reports 17 passed,
0 failed/skipped/blacklisted; the desktop-interaction wrapper reports 23 passed,
0 failed/skipped/blacklisted. Core Debug and Release passed 13/13 each. Core and desktop format/lint all
passed with no project warnings. Standard desktop application/helper targets
were refreshed successfully.


Standard application/helper targets also built successfully with current source:

```powershell
cmake --preset desktop-debug
cmake --build --preset desktop-debug --target fileweaver-desktop --parallel 4
cmake --preset desktop-release
cmake --build --preset desktop-release --target fileweaver-desktop --parallel 4
```


On 2026-11-12 at 04:29 UTC (12:29 Asia/Taipei), the previous PDF run still
reported queued with zero jobs/check runs since 2026-11-11 20:28:12 UTC, over
eight hours earlier. No hosted pass/failure or cause can be inferred from that
state. A new branch push will request checks for this follow-up source; keep
hosted acceptance pending until actual jobs execute. BMP and hideEvent do not
add dependencies, schema, permissions or runtime network behavior.


## First hosted attempt and checkout fix

Revision `6daf0e35c9c3739e3acc00bdcf76264e25283a05`,
[run 43155314316](https://github.com/Rex60309/FileWeaver/actions/runs/43155314316):
Windows core, Ubuntu core and analysis/portable-core ASan/UBSan passed. Desktop
job 130973775054 failed during dependency installation, before compiling the
application: Git rejected export-unofficial-poppler.patch at line 25 as corrupt.

Reproduction: converting the same patch to CRLF made git apply --check fail at
line 25 locally. Empty context lines in these patches must remain LF. Added a
narrow .gitattributes rule, `*.patch text eol=lf`; no application source, limits,
parser archive, dependency version or test is changed by this fix. Existing
application builds/tests/format/lint remain applicable; checkout-specific evidence:

```powershell
# Before fix: CRLF copy reproduced corrupt patch at line 25 (exit 1).
git -c core.autocrlf=false apply --check --directory=build/poppler-26.11.0 build/poppler-export-crlf.patch
# After staging .gitattributes, fresh isolated index checkout with Windows conversion:
git -c core.autocrlf=true checkout-index --all --prefix=build/windows-patch-checkout/
# Both checked-out patches contained no CR bytes and these checks passed (exit 0):
git -c core.autocrlf=false apply --check --directory=build/poppler-26.11.0 build/windows-patch-checkout/ports/poppler/export-unofficial-poppler.patch
git -c core.autocrlf=false apply --check --directory=build/poppler-26.11.0 build/windows-patch-checkout/ports/poppler/private-namespace.patch
```

The initial failure is not waived. A corrected fresh CI build is required for
MSVC acceptance. No application tests or warnings were disabled to repair it.
