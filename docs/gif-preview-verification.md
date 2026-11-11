# Windows GIF first-frame preview verification

Date: 2026-11-12 (Asia/Taipei). Scope explicitly authorized by the user: GIF
first-frame static thumbnails only, manual preview opt-in, existing resource/cache
limits, no new package/schema, no PDF or animation playback. ADR-0036 records the
internal protocol flag, bounded preflight and deployment decisions.

## Changes and acceptance

- Deploy the already-installed Qt QGifPlugin alongside the JPEG plugin.
- Helper explicitly selects GIF and decodes one frame. A bounded scan of header,
  global color table and extension sub-blocks validates canvas/first-image geometry
  before decoding; no later image descriptors are traversed.
- Preserve 8 MiB input, 8192 dimensions, 16,777,216 pixels, helper memory/CPU/wall/IPC
  limits and checksum-validated memory-only cache. No animation frame counting.
- Image and cached replies carry a validated optional boolean first_frame. All GIF
  previews display the static/first-frame label, including cache hits.
- Existing open confirmation/allowlist, preview off defaults and file guards stay.

## Generated fixtures and observed results

The generated two-frame 1x1 red/blue GIF is independently decoded in the test to
confirm both frames exist. The actual helper returns only red. Tests cover static
GIF87a, GIF89a, Unicode filename, unchanged source bytes, cached label/hash change,
invalid protocol flag, incomplete headers/color tables/extensions/descriptors,
missing first frame, zero/oversized canvas, pixel cap, oversized/out-of-canvas
first frame, input-size cap and cancellation. Existing process timeout/protocol,
stale-result, clear-cache, GUI opt-in and non-GIF preview regressions also run.

Regression discovered during implementation: Qt may report clipped first-frame
geometry instead of the oversized declared canvas/frame. Dimension tests failed
before the bounded header/descriptor preflight and passed after it. No test limit
was relaxed. Initial direct test invocation used the restricted environment's
unwritable default temporary folder; reruns explicitly use build/test-temp.

Windows 11, GCC 13.2.0, Qt 6.11.3, CMake 3.29.2. Local results so far:

| Gate | Observed result |
| --- | --- |
| Core Debug / Release full CTest | 13/13 each |
| Desktop Debug / Release full CTest | 15/15 each |
| GIF focused fixture | Passed |
| Core format/lint | Passed |
| Desktop format/lint | Passed |
| Hosted Windows/MSVC and sanitizer CI | All four jobs passed for corrected revision 2330115 |

## Exact commands

Existing dependency tree is pinned and reused without package downloads. Because
old generated test executables remain locked by terminated Windows records (see
Phase 2 ledger), fresh preset-based GIF directories contain the test builds:

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
cmake --preset desktop-debug -B build/gif-debug @dependencyOptions
cmake --build build/gif-debug --parallel
cmake --preset desktop-release -B build/gif-release @dependencyOptions
cmake --build build/gif-release --parallel
$env:TEMP = Join-Path $PWD 'build/test-temp'
$env:TMP = $env:TEMP
ctest --test-dir build/gif-debug --output-on-failure
ctest --test-dir build/gif-release --output-on-failure
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
cmake --preset desktop-debug
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
cmake --build --preset desktop-debug --target fileweaver-desktop --parallel
cmake --preset desktop-release
cmake --build --preset desktop-release --target fileweaver-desktop --parallel
```

The usual application directories are updated with helper and GIF plugin. Tests
requiring generated junctions/hard links run with permission to create those
fixtures. Combined ASan/UBSan is unsupported by the configured Windows compiler;
use the existing supported hosted portable-core sanitizer job and do not claim
Windows Qt sanitizer coverage. No new performance claim or benchmark is made.

## Safety and remaining work

No user source files, content logs, database schema or runtime network behavior
changed. GIF decoding remains within the existing resource-limited child; this is
not a complete permissions sandbox. Whole compressed GIF bytes are read within
the 8 MiB cap for checksum validation, although only frame one is decoded.

The desktop CI job time allowance is 120 instead of 90 minutes because the last
successful first-batch job consumed approximately 88 minutes; all verification
steps remain. Helper runtime limits are unchanged. Hosted verification and final
status/documentation updates remain pending. PDF and animation are excluded.

## Final local rebuild after integer-type cleanup

Static analysis rejected signed bitwise header operations. Little-endian dimension
reads now use bounded integer arithmetic, and table-size shifts use unsigned
operands; no warning suppression was added. The old gif-debug test executable was
again locked by Windows during relinking. Fresh directories built from the same
presets/dependencyOptions above use build/gif-final-debug and build/gif-final-release.
Both final full CTest runs passed **15/15**. Core Debug/Release remained **13/13**.
The usual desktop application targets rebuilt successfully after this cleanup.
Final desktop format and static analysis passed after this cleanup.

## Historical initial hosted checkpoint

Source revision `a331beabab52b83fdb8944e5bf5d07e1801d5dd9` was pushed to
`feat/windows-gif-preview`. [Quality run 43062161091](https://github.com/Rex60309/FileWeaver/actions/runs/43062161091)
has passed Windows core Debug/Release (130667334632), Ubuntu core
(130667334187), and analysis/ASan/UBSan (130667334744). Windows desktop
(130667334673) is still preparing Qt dependencies at this checkpoint. Final
acceptance remains pending until its MSVC Debug/Release builds and tests finish.

## Hosted MSVC warning and correction

Run 43062161091 passed its three core/analysis jobs, but desktop compilation
stopped before tests: MSVC C4334 (32-bit shift result widened to 64 bits) was an
error under /WX at the GIF color-table calculation. The shift and multiplication
now use unsigned size_t explicitly, then convert the bounded result (6..768 bytes)
to qsizetype. No warning setting or resource limit changes. Rebuild/retest and
hosted verification of the correction are required; the failed run is not accepted.

Correction local evidence: preset-based fresh build/gif-msvc-fix-debug and
build/gif-msvc-fix-release builds passed. Exact full test commands:

```powershell
ctest --test-dir build/gif-msvc-fix-debug --output-on-failure
ctest --test-dir build/gif-msvc-fix-release --output-on-failure
```

Both passed **15/15** after the MSVC shift-type correction, with TEMP/TMP explicitly
set to build/test-temp. Primary desktop application targets also rebuilt. The
helper arithmetic change does not affect core source or the prior core 13/13
results. Final format/lint passed before the correction commit.

## Historical correction hosted checkpoint

Source revision `233011552cc6570ff2857a1df8cb6ecb31f9e44b` is under verification in
[quality run 43073266927](https://github.com/Rex60309/FileWeaver/actions/runs/43073266927).
Windows core (130706449943), Ubuntu core (130706449686), and analysis/ASan/UBSan
(130706449290) passed. Desktop job 130706449786 is preparing Qt dependencies;
MSVC correction acceptance remains pending until both desktop configurations pass.

## Final acceptance — 2026-11-12

[Quality run 43073266927](https://github.com/Rex60309/FileWeaver/actions/runs/43073266927)
completed successfully for **233011552cc6570ff2857a1df8cb6ecb31f9e44b**:

| Job | Actual result |
| --- | --- |
| Windows desktop 130706449786 | MSVC Debug/Release builds and CTest **15/15 each** |
| Windows core 130706449943 | Debug/Release build and complete tests passed |
| Ubuntu core 130706449686 | Debug/Release build and complete tests passed |
| Analysis 130706449290 | Format, lint and supported Clang ASan/UBSan passed |

Desktop log records 15/15 at 2026-11-11 18:59:01 UTC (Debug) and 19:01:34 UTC
(Release). Its fresh checkout confirms the MSVC C4334 correction and actual GIF
plugin deployment independently of the local generated-executable lock issue.
The prior failed run remains documented above and is not counted as acceptance.

The authorized Windows GIF first-frame milestone is complete. README usage and
feature status now match the verified implementation. No user files, database
schema, package manifest, runtime networking or activation defaults changed.
Source GIF bytes remain read-only; no content is persisted by preview. Limits and
process/permissions-isolation limitations remain as documented. PDF, GIF animation,
Linux desktop and Phase 3-5 are not part of this milestone. A separately authorized
PDF preview design is the next smallest product extension.

The final acceptance update is documentation-only and uses `[skip ci]`: executable
source, tests, manifest and workflow remain identical to the verified revision.
No failed test is bypassed, no release package is created, and this is not a
formal release-readiness claim.
