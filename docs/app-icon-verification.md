# Windows app icon verification

Date: 2026-11-12 (Asia/Taipei).
Scope: explicitly requested desktop logo and current usage explanation, Windows
Phase 2 presentation only. No new packages, public search options, schema,
source-file mutation, content collection or runtime network behavior.

## Implementation

Original editable SVG and PNG/ICO product assets live in apps/desktop/assets.
Qt embeds the PNG at :/branding/fileweaver.png and QApplication sets the global
window icon. The desktop executable embeds ICO resource 101 using the existing
Windows resource compiler. ICO contains seven sizes from 16 to 256. PNG embedding
avoids adding a Qt SVG runtime dependency; helper/CLI behavior is unchanged.

Native resource inspection uses LoadLibraryEx(LOAD_LIBRARY_AS_DATAFILE), LoadImage
for icon 101 and System.Drawing.Icon bitmap export. Debug icon extraction succeeded
at 16, 32, 48 and 256 pixels, with matching dimensions; the extracted 256 image
was visually inspected. Qt's generated .qt/rcc/fileweaver_branding.qrc resolves
the /branding prefix and fileweaver.png alias to the bundled asset. An initial
inspection looked under .rcc rather than .qt/rcc; the corrected resource path was
inspected. No test or compiler check was weakened.

## Executed local gates

```powershell
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel 4
ctest --preset desktop-debug --output-on-failure
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel 4
ctest --preset desktop-release --output-on-failure
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
```

Tests use build/test-temp via TEMP/TMP. All local gates passed: core Debug and
Release 13/13 each, desktop Debug and Release 15/15 each, core/desktop format
and lint exit 0 with no project warnings. Release resource 101 also loaded
successfully at 16/32/48/256; Qt generated resource prefix/alias validation passed.
Native images at 256 and 32 were visually inspected. Both user-facing desktop
builds now include the icon. Hosted MSVC validation is not yet claimed.
Windows combined ASan/UBSan is unsupported by this local toolchain; no Windows
GUI sanitizer claim. No performance claim or release package. Native and Qt
resource compilation is included in both desktop builds above.
