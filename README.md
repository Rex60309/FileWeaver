# FileWeaver

[繁體中文](#繁體中文) | [English](#english)

![FileWeaver](apps/desktop/assets/fileweaver.svg)

## 繁體中文

FileWeaver 是用 **C++20、Qt 6 與 SQLite** 開發的檔案搜尋工具，提供 Windows 視窗介面和 CLI。選好資料夾、建立索引後，就能用檔名或路徑搜尋，不用自己操作資料庫。搜尋在本機完成，檔案不會上傳。

### 功能

- 檔名／路徑搜尋：精確、包含文字、前綴、Glob 與模糊比對，支援中文、日文及 emoji。
- 可勾選多種檔案類型，再依大小、資料夾或修改時間篩選及排序；最多顯示 1000 筆。
- 在背景建立或更新索引，需要時可以取消；也能開啟資料夾監控，自動更新一個根目錄的索引。
- 文字／程式碼、PNG／JPEG／BMP、GIF 首張及 PDF 首頁預覽。
- 用鍵盤選取結果、複製路徑，或確認後開啟檔案、在檔案總管定位。也可設定全域快捷鍵叫出視窗。

### 使用

1. 建置後開啟 `build/desktop-release/fileweaver-desktop.exe`。
2. 按「加入搜尋資料夾…」，選擇本機資料夾並確認建立索引。
3. 輸入關鍵字或留空瀏覽，用上方類型選項篩選。
4. 手動勾選內容預覽，選取檔案即可查看右側內容。

FileWeaver 不會改動來源檔案。預覽、監控和快捷鍵都要在開啟程式後手動啟用；要更新索引，可以重新加入同一個資料夾。預覽會限制檔案大小與資源用量，目前一次只能監控一個根目錄。

## English

FileWeaver is a file search tool built with **C++20, Qt 6 and SQLite**. It has a Windows desktop interface and a CLI. Select a folder, build its index, and search by filename or path. The app manages the database for you, and files stay on your computer.

### Features

- Filename/path search with exact, substring, prefix, glob and fuzzy matching; Unicode filenames including CJK and emoji.
- Select file types and filter or sort by size, folder and modification time; show up to 1000 results.
- Build or refresh indexes in the background, cancel a scan, or monitor one root folder for changes.
- Text/source, PNG/JPEG/BMP, GIF first-frame and PDF first-page previews.
- Browse results with the keyboard, copy paths, open files or locate them in Explorer after confirmation, and set a global hotkey.

### Usage

1. After building, open `build/desktop-release/fileweaver-desktop.exe`.
2. Choose **加入搜尋資料夾…** (Add search folder), select a local folder and confirm indexing.
3. Enter a query or leave it empty to browse, then filter using the toolbar.
4. Enable content preview and select a file to view it on the right.

FileWeaver leaves source files unchanged. Enable previews, monitoring and the hotkey manually after opening the app. Re-add the same folder to refresh its index. Previews have file-size and resource limits, and monitoring handles one root at a time. The interface currently uses Traditional Chinese labels.

## Build / 建置

建置前請準備 C++20 編譯器、CMake 3.25+、Ninja、Python 3 和 vcpkg，並將 `VCPKG_ROOT` 指向 vcpkg 目錄。使用 Windows/MSVC 時，請在開發者命令提示字元或開發者 PowerShell 中執行。
Requires a C++20 compiler, CMake 3.25+, Ninja, Python 3 and vcpkg. Set `VCPKG_ROOT`; use a developer shell for Windows/MSVC.

```powershell
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel
ctest --preset desktop-release --output-on-failure
```

僅建置 CLI 時改用 `release` preset。首次桌面建置需下載／編譯 Qt、Poppler 等套件。執行時保留相依 DLL、Qt 外掛與 `fileweaver-preview-worker.exe`。
For CLI-only builds, use the `release` preset. The first desktop build downloads/compiles Qt, Poppler and other dependencies. Keep the adjacent DLLs, Qt plugins and preview worker with the executable.

### CLI

```powershell
.\build\release\fileweaver.exe init --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe index "D:\Example" --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe search "report" --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe --help
```

## Performance / 本機量測

測試環境: Windows 11、i7-13700KF、約 32 GiB RAM、NVMe SSD，Release 建置。使用本機產生的合成資料，快取未清除。
Test Environment: Windows 11, i7-13700KF, about 32 GiB RAM, NVMe SSD, Release build. The datasets are synthetic and caches were not cleared.

| 項目 / Metric | 1 萬 / 10k | 10 萬 / 100k |
| --- | ---: | ---: |
| CLI 精確搜尋 / Exact search — p95 | 29.15 ms | 27.45 ms |
| CLI 子字串搜尋 / Substring search — p95 | 35.58 ms | 75.71 ms |
| CLI 模糊搜尋 / Fuzzy search — p95¹ | 30.83–35.39 ms | 91.90–107.49 ms |
| 首次建索引 / Initial indexing² | 8.50 s | 114.17 s |
| 未變動重掃 / Unchanged rescan² | 8.10 s | 112.31 s |
| 變動後重掃 / Rescan after changes² | 8.12 s | 111.69 s |
| CLI 峰值記憶體 / Peak working set³ | 11.91 MiB | 11.91 MiB |

搜尋每個案例測量 30 次，時間包含 CLI 啟動與 JSON 處理。¹ 模糊搜尋列出四個案例各自 p95 的範圍，另含記憶體觀測成本。² 索引列出三輪的中位數；變動包含新增、修改、改名、刪除各 10 個檔案。³ 記憶體是另測六次查詢的程序峰值，不是 GUI 用量。
Each search case ran 30 times, including CLI startup and JSON processing. ¹ Fuzzy values span the p95 of four cases and include memory-observer overhead. ² Index values are medians of three rounds; changes comprise 10 creations, updates, renames and deletions each. ³ Memory is the peak from six extra CLI searches, not GUI memory.

索引重掃仍接近首次建立的時間，這部分值得後續分析。上述數字只適用於這組測試，不代表 GUI 延遲。
Rescans took about as long as initial indexing and are worth profiling next. These results apply to this test setup and do not measure GUI latency.

[完整數據、條件與重現指令 / Full results and reproduction steps](docs/performance/windows-20261113.md)

## Status / 狀態

目前本機 Debug 和 Release 都已通過核心 13 項、桌面 16 項測試，以及格式與靜態檢查。最新介面還沒跑遠端測試，Actions 目前暫停。若要使用程式，請先自行建置，目前還沒有獨立安裝包。
Both local Debug and Release builds passed all 13 core tests and 16 desktop tests, along with formatting and static checks. The latest UI changes have not run remote tests because Actions is paused. Build from source to try the app; an installer is not available yet.

相依套件與散布授權注意事項見 [Dependency overlays](ports/README.md)。
See [Dependency overlays](ports/README.md) for dependency and redistribution licensing notes.

[Architecture / 架構](docs/architecture.md) · [Benchmarks / 量測紀錄](docs/performance/)
