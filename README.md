# FileWeaver

[English](README.en.md) | 繁體中文

![FileWeaver](apps/desktop/assets/fileweaver.svg)

以 **C++20、Qt 6 與 SQLite** 開發的本機檔案搜尋工具，提供 Windows 桌面介面與 CLI。
加入資料夾後即可搜尋，不需自行建立資料庫。搜尋離線運作，不上傳檔案。

## 現有功能

- **檔名與路徑搜尋**：精確、包含文字、前綴、Glob、模糊檔名與模糊路徑搜尋；支援中文、日文及 emoji 檔名。
- **篩選與排序**：橫排複選檔案類型，搭配大小、根目錄、修改時間與排序條件；預設顯示 100 筆，可調至 1000 筆。
- **背景建索引**：在介面加入資料夾、取消或重掃；索引由程式保存在本機，也可選用既有 SQLite。
- **內容預覽**：文字／程式碼語法上色、PNG／JPEG／BMP、GIF 首張與 PDF 首頁；圖片依面板縮放並支援 JPEG 方向資訊。
- **日常操作**：鍵盤導覽、複製路徑、確認後開啟檔案或顯示所在位置；可選用全域快捷鍵與單一根目錄監控。
- **CLI**：建立與更新索引、搜尋、列出根目錄及檢查索引狀態，支援 JSON 輸出。

## 開始使用

1. 建置後開啟 `build/desktop-release/fileweaver-desktop.exe`。
2. 按「加入搜尋資料夾…」，選擇本機資料夾並確認建立索引。
3. 輸入關鍵字或留空瀏覽，使用清單上方的類型與筆數控制；收起設定區可顯示更多檔案。
4. 勾選「啟用內容預覽」後，選取檔案查看右側預覽。

目前開發工作目錄因 Windows 鎖定舊執行檔，最新版名稱是 `fileweaver-desktop-updated.exe`；一般建置仍使用原名稱。
重新加入已登錄資料夾可更新索引；也可手動啟用監控。程式關閉後，全域快捷鍵無法啟動它。

### CLI 範例

```powershell
.\build\release\fileweaver.exe init --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe index "D:\Example" --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe search "report" --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe refresh --database ".\build\catalog.sqlite"
.\build\release\fileweaver.exe --help
```

## 建置

需要 C++20 編譯器、CMake 3.25+、Ninja、Python 3 與 vcpkg。
將 `VCPKG_ROOT` 指向已安裝的 vcpkg；Windows/MSVC 請在開發者命令環境執行：

```powershell
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel
ctest --preset desktop-release --output-on-failure
```

僅建置 CLI 時改用 `release` preset。第一次桌面建置會下載並編譯 Qt、Poppler 等相依套件，耗時較長。
執行時請保留同目錄的 DLL、Qt 外掛及 `fileweaver-preview-worker.exe`；目前沒有獨立安裝包。

## 使用範圍與驗證

來源檔案維持唯讀；開啟外部程式需確認。預覽、監控及快捷鍵每次啟動預設關閉。
索引不會在啟動時自動重掃；預覽有大小與資源限制，多根目錄同時監控尚未提供。
桌面介面目前以 Windows 為主。

本機核心 Debug／Release 各 13/13、桌面各 16/16 通過，格式與靜態檢查通過；最新介面變更尚未執行遠端驗證。
GitHub Actions 目前已暫停，push 不會執行遠端建置／測試；本機驗證不受影響。

- [功能狀態](docs/feature-status.md) · [驗證命令與紀錄](docs/search-layout-verification.md)
- [十萬檔案量測](docs/performance/phase1-100k-20261109.md)：歷史版本、合成資料與暖快取觀測，不能視為目前版本或所有環境的速度保證。
- [產品規格](PRODUCT_SPEC.md) · [設計決策](docs/decisions) · [PDF 相依套件與授權](docs/decisions/ADR-0037-pdf-first-page-preview.md)

Git 保留原始碼、測試、建置設定、圖示與經整理的量測文件；忽略建置產物、資料庫、快取及本機設定。
