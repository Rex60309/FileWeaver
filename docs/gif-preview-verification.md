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
| Hosted Windows/MSVC and sanitizer CI | Not yet run for this revision |

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
