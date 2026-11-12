# FileWeaver

English | [繁體中文](README.md)

![FileWeaver](apps/desktop/assets/fileweaver.svg)

A local file search application built with **C++20, Qt 6 and SQLite**, with a Windows desktop UI and CLI.
Add a folder and search without preparing a database yourself. Search works offline and does not upload files.

## Features

- **Filename and path search**: exact, substring, prefix, glob, fuzzy filename and fuzzy path matching; Unicode filenames including CJK and emoji.
- **Filters and sorting**: a horizontal multi-select file-type toolbar, size, root and modification-time filters; 100 results by default, adjustable to 1000.
- **Background indexing**: add folders, cancel or rescan from the UI; automatically managed local storage or an existing SQLite index.
- **Content previews**: text/source syntax highlighting, PNG/JPEG/BMP, GIF first frames and PDF first pages; fitted images with JPEG orientation support.
- **Daily navigation**: keyboard controls, path copying, confirmed external opening and Explorer reveal; optional global hotkey and single-root monitoring.
- **CLI**: index, refresh, search, inspect roots and check catalog status, with JSON output.

## Quick start

1. After building, open `build/desktop-release/fileweaver-desktop.exe`.
2. Choose **加入搜尋資料夾…** (Add search folder), select a local folder and confirm indexing.
3. Enter a query or leave it empty to browse. Use the type and result-limit controls above the list; collapse settings for more space.
4. Enable **啟用內容預覽** (Content preview) and select a file to preview it on the right.

The current development workspace uses `fileweaver-desktop-updated.exe` because Windows locked the previous image; ordinary builds retain the original name.
Re-add a registered folder to refresh it, or enable monitoring manually. The global hotkey cannot start the application after it has exited.
The desktop interface currently uses Traditional Chinese labels.

### CLI examples

```powershell
.\build\release\fileweaver.exe init --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe index "D:\Example" --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe search "report" --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe refresh --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe --help
```

## Build

Requires a C++20 compiler, CMake 3.25+, Ninja, Python 3 and vcpkg.
Set `VCPKG_ROOT` to an installed vcpkg checkout. For Windows/MSVC, use a developer shell:

```powershell
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel
ctest --preset desktop-release --output-on-failure
```

Use the `release` preset for CLI-only builds. The first desktop build downloads and compiles dependencies including Qt and Poppler, which can take substantial time.
Keep the adjacent DLLs, Qt plugins and `fileweaver-preview-worker.exe` with the application. No standalone installer is provided.

## Scope and verification

Source files remain read-only; external opening requires confirmation. Preview, monitoring and hotkey activation default to off on every launch.
Startup does not rescan sources automatically. Previews have size/resource limits; simultaneous multi-root monitoring is not implemented.
The desktop application currently targets Windows.

Local core Debug/Release tests passed 13/13 each and desktop tests passed 16/16 each, with format and static checks passing. The latest UI changes have not run hosted verification.
GitHub Actions is currently paused: pushes do not run hosted builds/tests. Local verification remains available.

- [Feature status](docs/feature-status.md) · [Verification commands and evidence](docs/search-layout-verification.md)
- [100k-file benchmark](docs/performance/phase1-100k-20261109.md): historical source, synthetic data and warm-cache observations, not a current-version or universal performance guarantee.
- [Product specification](PRODUCT_SPEC.md) · [Architecture decisions](docs/decisions) · [PDF dependency and licensing](docs/decisions/ADR-0037-pdf-first-page-preview.md)

Git retains source, tests, build configuration, branding assets and curated benchmark documentation; generated builds, databases, caches and local settings are ignored.
