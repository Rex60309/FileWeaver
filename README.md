# FileWeaver

以 C++20 開發的本機檔案搜尋與生產力工具。**Phase 1 — Search Core 已完成實作與驗收**，
範圍依 SPEC 第 24 節；尚無正式發行版本。Phase 2 已加入可選的 Windows Qt 搜尋視窗，
支援選擇既有索引、背景搜尋、鍵盤選取與手動複製完整路徑。其餘 Phase 2 功能仍為規劃項目。
已可將指定目錄的檔案中繼資料寫入 SQLite 並重掃更新；
已可搜尋仍存在於索引中的檔名，支援精確、子字串與前綴比對。

## 目前有哪些功能？

| 功能          | 目前行為                                                     |
| ------------- | ------------------------------------------------------------ |
| Windows 搜尋視窗 | 選擇既有索引，包含文字／檔名開頭／完整檔名比對，最多 100 筆；方向鍵選取及複製路徑 |
| CLI 使用說明  | 不帶參數、`--help` 或 `-h` 顯示說明                      |
| SQLite 初始化 | `init --database <路徑>` 建立 schema v3，包含索引與每個根目錄的掃描設定 |
| 重複初始化    | 檢查既有 schema，保留 settings 資料                          |
| 檔案索引 | `index <root> --database <路徑>` 保存檔名、路徑、大小及修改時間 |
| 指定根目錄重掃 | 再次執行 index，辨識新增、修改及路徑變動；安全條件符合時標記 Missing |
| 批次更新根目錄 | `refresh --database <路徑>`，重用已保存的排除規則與深度設定 |
| 唯讀檢查 | `roots` 列出根目錄與設定；`status` 顯示索引紀錄數，不掃描來源 |
| 開發用 benchmark | 合成資料、三種基準、可選 C++ 核心計時與程序記憶體觀測、逐次正確性驗證 |
| 檔名搜尋 | `search <query> --database <路徑>`，支援 exact／substring／prefix 與結果數量限制 |
| 多語言檔名 | 中文、日文、重音字元與 emoji 可用於檔名、目錄、資料庫路徑及搜尋；Windows 主控台使用 Unicode 輸出 |
| Windows 建立時間 | 索引可取得的原生建立時間；未知或超出範圍時使用空值，不推測為修改時間 |
| 搜尋篩選 | 副檔名、邏輯大小上下限、指定已登錄根目錄；可組合使用 |
| 修改時間篩選 | `--modified-after`／`--modified-before`，以明確 UTC 時間比對索引 |
| 搜尋排序 | `--sort name|size|modified`，可搭配 `--descending` |
| JSON 輸出 | init／index／search／refresh／roots／status／help 支援 `--json`；stdout 僅一份 JSON，診斷留在 stderr |
| 錯誤回報      | 對無效參數、無法開啟資料庫、損毀或不相容 schema 回報錯誤碼   |
| 開發工具      | Debug／Release 建置、測試、格式檢查與靜態分析已在本機驗證    |

開發者可使用 `fileweaver_filesystem` 模組中的 `scan_directory`，串流取得指定
根目錄下的檔案中繼資料，支援排除路徑、取消與錯誤統計。它不讀取檔案內容、
不跟隨 symlink／Windows reparse point；深度預設最多 128 層 iterator，超過會
標記掃描不完整。目前不支援 Windows UNC／裝置／延伸路徑命名空間，也不能保證
抵擋惡意目錄替換競態。CLI 的 `index` 指令使用這個共用核心；沒有獨立 `scan` 指令。

**尚未實作：**glob／模糊／路徑搜尋、根目錄移除／索引清除指令、全域快捷鍵、
檔案監控與預覽、重複檔案分析、整理與重新命名、剪貼簿歷史、專案啟動器、
語意搜尋、備份與還原。這些是規劃項目，不能透過目前版本使用。

## Windows 搜尋視窗怎麼使用？

先使用下方 CLI 的 `init`、`index` 建立索引，再執行：

```powershell
.\build\desktop-release\fileweaver-desktop.exe
# 或直接指定既有索引
.\build\desktop-release\fileweaver-desktop.exe --database ".\build\my-index\index.sqlite"
```

1. 按「選擇索引…」選取既有 `.sqlite` 或 `.db`。
2. 輸入檔名，選擇包含文字、檔名開頭或完整檔名；輸入稍停後自動搜尋，也可按 Enter／搜尋。
3. 按 ↓ 進入結果，↑／↓ 選取。「複製完整路徑」或結果區的 Ctrl+C 可複製選取路徑，Esc 關閉視窗。

搜尋使用索引快照，最多顯示 100 筆；比對規則沿用 CLI：完整檔名區分大小寫，
包含文字／檔名開頭僅忽略 ASCII 大小寫；不做完整 Unicode 大小寫折疊或正規化。輸入或索引改變會取消舊查詢並清除舊結果。
視窗不會建立索引、重掃來源、開啟檔案或讀取檔案內容，資料變更請先用 CLI 更新索引。
選擇大型目錄不會觸發掃描。剪貼簿只在你明確按下複製時寫入，不收集歷史。

### 建置視窗（Windows 開發者）

在已設定 C++20、CMake、Ninja、Python 與 vcpkg 的環境執行：

```powershell
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel
ctest --preset desktop-debug --output-on-failure
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel
ctest --preset desktop-release --output-on-failure
```

這兩個 preset 開啟 `FILEWEAVER_BUILD_DESKTOP` 與 vcpkg 的 `desktop` feature，
首次會下載並編譯 Qt 6 Widgets／Test 及其依賴；預設 CLI preset 不需要 Qt。
若 PCRE2 依賴下載因 GitHub API 的 JSON 排版變動而發生 SHA512 不符，可先執行
`python tools/prepare_qt_download.py` 再重新設定；此步驟仍要求符合固定版本的原始雜湊，不會略過驗證。
MinGW 的編譯器、資源編譯器與依賴必須使用相同架構；不要混用 32 位元 windres 與 64 位元 GCC。
執行時需保留建置目錄中的相依 DLL、`platforms` 與 `styles` 子目錄，不要只複製 EXE。
目前沒有提供可攜式發行 ZIP，部署到其他電腦與正式發行仍需另行驗證及整理授權文件。
驗證範圍與限制見 [Windows 視窗驗證紀錄](docs/desktop-search-verification.md)。

### 哪些檔案會上傳 Git？

原始碼、測試腳本與建置設定保留在 Git；成品、依賴及測試輸出集中在忽略的 `build/`。
`.gitignore` 另排除其他 `build-*` 目錄、Python 快取、SQLite 資料庫與日誌、`.env` 等本機設定。
測量報告請輸出至 `build/`，不要把真實檔案清單或私人路徑提交；正式發行成品應透過 GitHub Releases 提供。
忽略規則不會移除已追蹤檔案，提交前仍需檢查差異。

## 如何使用（Windows PowerShell）

檔名不限定英文或數字。中文、日文與空白可以直接使用，路徑或查詢請加上引號，例如：

```powershell
.\build\debug\fileweaver.exe search "日本語" --database "D:\索引\檔案.sqlite"
.\build\debug\fileweaver.exe search "報告_日本語 (完成)-é😀.txt" --mode exact --database "D:\索引\檔案.sqlite" --json
```

請先用 `init` 建立資料庫，再用 `index` 索引指定目錄（完整步驟如下）。
若先前因多語言參數失敗，更新執行檔後重新執行原指令即可，不需更改檔名或資料庫格式。
Windows 原生主控台直接使用 Unicode API，不修改系統或主控台編碼；管線與檔案輸出維持 UTF-8。
字形（尤其 emoji）的外觀仍取決於終端機及字型。比對不做 Unicode 正規化或完整大小寫折疊；
例如不同組合形式的重音字元仍視為不同字串。作業系統原有的檔名限制仍適用。

若索引時出現 `Cannot convert character sequence: Illegal byte sequence`，代表某個
路徑在字元轉換時失敗，不能僅憑這句話判定是哪個檔案或是否損壞。日文、全形括號、
`〖〗⌇⁉…♡×` 等合法字元已有合成子目錄測試。更新後，儲存層遇到此轉換失敗會顯示
`Cannot encode filesystem path as UTF-8`，並附上 `Path (escaped native code units)`；
例如 `invalid-\uD800.txt` 可定位含有不完整 Unicode 的項目。這是診斷表示法，不是要求
把檔名改成該文字。程式不自動改名或略過；失敗時本次檔案索引更新回滾，既有資料保留。
請提供該錯誤行協助定位（私人路徑可遮掉），不需先刪除資料庫。

先切換到專案根目錄。若已完成建置，可直接執行以下指令；若找不到執行檔，
請先依下方 Build and verify 完成建置。目前沒有安裝程式；可選桌面視窗的建置與操作方式見上節。

### 1. 查看說明

```powershell
.\build\debug\fileweaver.exe --help
# 不帶參數或使用 -h 也會顯示說明
.\build\debug\fileweaver.exe
```

### 2. 初始化指定的資料庫

以下在系統暫存目錄建立一個新的示範資料夾，再建立資料庫。每次執行都使用
新的資料夾，不需選取或掃描任何既有個人檔案。

```powershell
$demoDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('fileweaver-demo-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $demoDirectory | Out-Null
$databasePath = Join-Path $demoDirectory 'index.sqlite'
.\build\debug\fileweaver.exe init --database "$databasePath"
$LASTEXITCODE
```

成功時顯示 `Database initialized.`，退出碼為 `0`。資料庫位於 `$databasePath`，
目前包含設定表與空的 roots、files、scans 表，不會掃描檔案、填入索引或產生搜尋結果。

也可改成你明確指定的資料庫檔案路徑；父資料夾必須已存在。路徑支援空白與
Unicode，請保留引號。初始化會寫入該資料庫，SQLite 也可能建立 `-wal`、`-shm`
附屬檔案；請使用 FileWeaver 專用資料庫，不要指定其他應用程式的資料庫。

### 3. 重新檢查同一個資料庫

```powershell
.\build\debug\fileweaver.exe init --database "$databasePath"
```

正常情況仍顯示 `Database initialized.`，保留既有 settings 資料。
若資料庫來自先前的 schema v1／v2，這個指令會以單一交易升級至 v3；任何建表或
schema 驗證失敗會回滾，不會留下半套升級。這是向前升級，沒有自動降版功能；
舊版只支援 v1／v2 的 FileWeaver 無法開啟 v3。請先自行保留重要資料庫的副本，再升級。
新版 search 仍可唯讀查詢 v2；index／refresh 需先升級至 v3。舊根目錄的排除規則
未曾保存，因此升級後必須明確執行一次 index（帶上所需 --exclude），才能 refresh。
可用 roots／status 檢查索引，尚無設定編輯或根目錄移除指令。Release 執行檔使用方式相同，
只需將路徑中的 `debug` 換成 `release`。

### 4. 建立及更新檔案索引

接續前面的 `$demoDirectory` 與 `$databasePath`，建立合成示範資料並索引：

```powershell
$sourceDirectory = Join-Path $demoDirectory 'source'
New-Item -ItemType Directory -Path $sourceDirectory | Out-Null
Set-Content -LiteralPath (Join-Path $sourceDirectory 'report.txt') -Value 'demo'
.\build\debug\fileweaver.exe index "$sourceDirectory" --database "$databasePath"
```

首次輸出範例（此資料夾只有一個示範檔案）：

```text
Indexed files=1 added=1 updated=0 missing=0 skipped=0 errors=0 status=completed
```

再執行相同 index 指令會重掃這個根目錄；未變更的紀錄不重寫，`added` 與
`updated` 為 0。Windows 舊紀錄若缺少建立時間，首次重掃可補齊並計入 `updated`；
之後未變更時不重寫。建立時間目前保存在索引內，CLI 沒有額外顯示或篩選欄位。
其他平台目前保留空值。要排除路徑，可重複使用 `--exclude`，相對路徑以掃描根目錄為準：

```powershell
.\build\debug\fileweaver.exe index "$sourceDirectory" --database "$databasePath" --exclude "cache" --exclude "build"
```

資料庫需先用 `init` 建立／升級；index 不會隱式建立資料庫。建議將資料庫放在
來源根目錄之外。程式會自動略過自己的資料庫、其 hard link 與 SQLite 附屬檔案。
只有完整、**沒有略過任何項目且沒有設定使用者排除路徑** 的掃描，才將未再出現的
紀錄標成 Missing；其他情況會保留舊紀錄並在 stderr 說明，因此可能留有過時紀錄。

取消或發生掃描錯誤時，本次檔案更新會回滾，既有索引保留；掃描狀態會記錄於
資料庫。取消目前提供給 C++ 呼叫端，CLI 尚未提供優雅的 Ctrl+C 取消處理。
重新命名以「新路徑＋舊路徑 Missing」表示；不讀取或比對檔案內容。
索引路徑目前採大小寫精確比對，大小寫不同的根目錄寫法可能形成不同根目錄。
`--root` 請使用 `roots` 列出的登錄路徑；Windows 短路徑或其他別名不保證匹配。
搜尋不會為了解析別名而存取來源磁碟，來源離線時仍可查詢索引。

### 5. 搜尋已索引的檔名

接續上面的示範資料庫：

```powershell
# 預設子字串搜尋，最多 100 筆
.\build\debug\fileweaver.exe search "report" --database "$databasePath"
# 精確檔名比對，最多 10 筆
.\build\debug\fileweaver.exe search "report.txt" --mode exact --limit 10 --database "$databasePath"
# 檔名開頭比對，可搭配現有篩選、排序與數量限制
.\build\debug\fileweaver.exe search "報告" --mode prefix --database "$databasePath"
```

每筆輸出為「邏輯位元組大小、Tab、加引號的完整路徑」；引號、反斜線及 ASCII
控制字元會跳脫。沒有結果時 stdout 為空，退出碼仍為 0。
預設以檔名升冪排序，再按正規化路徑、根目錄 ID、檔案 ID 決定順序；限制在排序之後套用。

`exact` 大小寫敏感；`substring` 與 `prefix` 只忽略 ASCII 大小寫，中文字以原字比對，
尚無完整 Unicode 大小寫折疊。`%`、`_`、引號都是普通字元，沒有萬用字元語法。
`prefix` 只比對檔名開頭，不比對完整路徑；預設仍為 `substring`。
目前前綴搜尋沒有專用索引加速，也沒有量測過的效能保證。
前綴模式已驗證重掃後排除 Missing、檔案重新出現時沿用 ID，以及唯讀查詢 v2
資料庫而不自動升級。核心 API 的預先取消會回報錯誤，之後可重新搜尋；這不代表
CLI 已支援 Ctrl+C 優雅取消。測試與限制見[前綴驗證紀錄](docs/prefix-search-verification.md)。
`--limit` 可設定為 1–10000；查詢不可為空，最多 4096 UTF-8 位元組。
回傳文字總量限制為 16 MiB，超過時回報錯誤，可降低 limit 後重試。

搜尋只查資料庫，不重新掃描或開啟來源檔案；檔案已刪除但尚未重掃／標記 Missing
時仍可能出現。多個重疊根目錄可能回傳同一路徑的多筆紀錄。任意子字串搜尋可能
掃過索引，目前沒有延遲或大型資料集效能保證。CLI 尚無優雅的 Ctrl+C 取消處理。

### 6. 組合搜尋篩選

```powershell
.\build\debug\fileweaver.exe search "report" --database "$databasePath" --ext txt --min-size 0 --max-size 1048576 --root "$sourceDirectory" --limit 20
```

以上只回傳指定根目錄內、最後副檔名為 `.txt`、大小介於 0 至 1048576 位元組
（含邊界）的檔名結果。所有條件以 AND 組合，先篩選與排序，再限制回傳筆數。

| 選項 | 規則 |
| --- | --- |
| `--ext txt` | 可寫 txt 或 .txt，忽略 ASCII 大小寫；比對最後副檔名，tar.gz 使用 gz |
| `--ext ""` | 明確傳入獨立空字串參數，只匹配沒有副檔名的檔案 |
| `--min-size 0` | 非負整數位元組，含下限；不接受 KB／MB 等單位字尾 |
| `--max-size 1048576` | 非負整數位元組，含上限；不能小於 min-size |
| `--root <路徑>` | 精確選取一個已登錄根目錄的索引紀錄，不是路徑前綴搜尋 |

所有篩選皆可省略。root 的相對路徑以目前工作目錄為準，允許 `.`、`..` 與尾端
分隔符正規化；大小寫須符合索引中的根目錄鍵。即使來源目錄已刪除或離線，也能
查詢其中尚未標為 Missing 的索引。未登錄的根目錄回傳空結果，不會自動新增根目錄。
根目錄別名（例如不同大小寫拼法）目前不會自動合併。篩選同樣不重新讀取來源檔案。

空副檔名和省略 `--ext` 是不同條件；目前空值須作為獨立參數傳入，終端機需保留該
空參數，不能用 `--ext=` 代替。明確傳入空的 `--root`、`--modified-after` 或
`--modified-before` 會回報參數錯誤（退出碼 2），不會悄悄取消篩選。

### 7. 依修改時間篩選

```powershell
.\build\debug\fileweaver.exe search "report" --database "$databasePath" --modified-after "2026-01-01T00:00:00Z" --modified-before "2027-01-01T00:00:00Z" --ext txt
```

只回傳修改時間嚴格晚於第一個時間、嚴格早於第二個時間的索引紀錄；邊界本身
不包含在結果內。兩個選項都可單獨使用，也可和副檔名、大小、根目錄與 limit 組合。

時間必須寫成 `YYYY-MM-DDTHH:MM:SSZ`，其中 `Z` 表示 UTC，不會套用本機時區。
接受整秒，不接受只有日期、時區位移（如 +08:00）、小數秒、閏秒或「今天」等相對
寫法。可表示的整秒範圍是 `1677-09-21T00:12:44Z` 至 `2262-04-11T23:47:16Z`。
日期不存在、超出範圍，或 after 不早於 before（包括相同）時，退出碼為 2。

比對的是索引中的修改時間，不是建立／開啟時間，也不會重新讀取來源檔案。
來源中繼資料可有次秒精度，因此 `00:00:00Z` 之後 1 奈秒的紀錄會符合 after；
CLI 輸入的邊界目前只接受整秒。

### 8. 排序搜尋結果

```powershell
# 最大的檔案優先
.\build\debug\fileweaver.exe search "report" --database "$databasePath" --sort size --descending --limit 10
# 最近修改的紀錄優先
.\build\debug\fileweaver.exe search "report" --database "$databasePath" --sort modified --descending --ext txt
```

`--sort` 可選 `name`（預設）、`size`（邏輯位元組）或 `modified`（索引中的修改時間）。
預設升冪，加上 `--descending` 只反轉主要排序欄位。相同值仍按檔名、正規化路徑、
根目錄 ID、檔案 ID 升冪排列，因此結果穩定。所有篩選先套用，再排序與限制筆數。
名稱使用二進位字串順序，不是語系排序。排序不重新讀取來源檔案，也沒有大型索引
延遲保證；SQLite 可能使用暫存空間進行排序。

### 9. JSON 輸出與腳本整合

```powershell
.\build\debug\fileweaver.exe search "report" --database "$databasePath" --sort size --descending --json
$result = .\build\debug\fileweaver.exe --json search "report" --database "$databasePath" | ConvertFrom-Json
$result.results
```

`--json` 可放在指令前或後，也適用於 init、index、說明及錯誤回報。stdout 只有一份
UTF-8 JSON 文件；警告與詳細診斷在 stderr，請不要將 stderr 合併進 JSON 管線。
程式退出碼維持不變，腳本應同時檢查 `$LASTEXITCODE` 與 `ok`／`exit_code`。

搜尋結果的欄位如下；沒有符合項目時 `results` 是空陣列、`count` 是 0：

```json
{"ok":true,"exit_code":0,"command":"search","results":[],"count":0}
```

每筆 results 包含 `file_id`、`root_id`、`path`、`name`、`size_bytes`。順序沿用搜尋的
排序／篩選／limit。數值是 JSON 整數，讀取器須保留 64 位元精度；以 JavaScript
Number 直接讀取很大的 ID 或大小可能失去精度。路徑保留 Unicode，控制字元、引號
及反斜線會跳脫；無效 UTF-8 文字會回報退出碼 1，不會悄悄替換路徑字元。

| command | 成功時的其他欄位 |
| --- | --- |
| `init` | 無；代表資料庫初始化／升級成功 |
| `help` | `text`：說明文字 |
| `index` | `scan`：files、skipped、errors、complete、cancelled；以及 added、updated、missing、missing_reconciled |
| `search` | `results` 陣列、`count` |
| `refresh` | `cancelled`、`roots`；每筆含 root_id、path、attempted、exit_code，以及可用的 index 摘要／error |
| `roots` | `roots` 陣列、`count`；每筆含保存的路徑、狀態與掃描設定 |
| `status` | `schema_version`、roots、present_files、missing_files、scan_records、running_scan_records |

一般錯誤格式（詳細原因在 stderr）：

```json
{"ok":false,"exit_code":4,"error":"Index not initialized"}
```

索引掃描不完整時退出碼為 1，JSON 保留 index 的統計，帶 `ok:false` 與
`error:"Scan incomplete"`；若直接發生例外，使用一般錯誤格式，不提供可能誤導的
部分計數。成功但延後 Missing 判定時仍為 `ok:true`，`missing_reconciled:false`，
並在 stderr 提示。`--` 後面的 `--json` 視為查詢文字，不會啟用 JSON 模式。

### 10. 更新所有已登錄根目錄

```powershell
.\build\debug\fileweaver.exe refresh --database "$databasePath"
.\build\debug\fileweaver.exe refresh --database "$databasePath" --json
```

refresh 只處理目前資料庫中已登錄的根目錄，不會探索其他資料夾，並沿用各根目錄
最近一次明確 index 保存的排除路徑與深度設定。**再次 index 不帶 --exclude 會將
保存的使用者排除規則改成空白**；要直接重用既有設定，請使用 refresh。

舊版資料庫先執行 init 升級到 v3，再逐個確認原有規則，例如：

```powershell
.\build\debug\fileweaver.exe init --database "$databasePath"
.\build\debug\fileweaver.exe index "$sourceDirectory" --database "$databasePath" --exclude "cache" --exclude "build"
.\build\debug\fileweaver.exe refresh --database "$databasePath"
```

未確認設定的舊根目錄會回報 `attempted:false` 與根目錄退出碼 5，不會假設沒有排除
規則而掃描。若設定在快照後被其他索引工作更改，該根目錄會失敗並要求重試，不會
覆寫新規則。根目錄已不存在／無權存取時保留舊索引，繼續更新其他根目錄。

每個根目錄分開提交；整批不是單一交易。任何根目錄失敗或取消時，整體退出碼為 1，
逐項結果仍會列出已成功、失敗與未處理項目。JSON 的 roots 內可用 index 欄位是完整
的 index 摘要物件。資料庫沒有根目錄時成功回傳空清單，不會開始全碟掃描。
缺少／不相容資料庫仍使用原有退出碼 4／5。

目前快照上限為 10000 個根目錄、合計 10000 條排除路徑與 16 MiB 路徑文字；超過
時會在掃描前回報錯誤。取消仍由 C++ API 提供，CLI 尚無優雅 Ctrl+C 處理。既有
Missing 判定、reparse point、Windows 路徑與競態限制維持不變。

### 11. 檢查根目錄與索引狀態

```powershell
.\build\debug\fileweaver.exe roots --database "$databasePath"
.\build\debug\fileweaver.exe status --database "$databasePath" --json
```

兩個指令都只查資料庫，不會掃描、開啟或確認來源檔案是否仍存在，也不會自動升級。
支援 v2／v3。根目錄依索引中的路徑鍵與 ID 固定排序；顯示排除規則、深度與最後
記錄的掃描狀態。`last_scan_at` 是 UTC Unix 奈秒整數，不是本機格式化日期。

roots 的 JSON 每筆含 root_id、path、last_scan_status、last_scan_at、policy_known、
maximum_depth、exclusions。未知時間／深度是 null。v2 或未確認規則的舊根目錄會
顯示 `policy_known:false`、`maximum_depth:null`；exclusions 空陣列此時**不代表已確認
沒有排除規則**，仍需明確 index 後才能 refresh。

status 的 `roots` 是登錄根目錄數；`present_files`／`missing_files` 是索引紀錄數，
不是即時檔案數，也不是去重後的實體檔案數。`scan_records` 是掃描歷史筆數，
`running_scan_records` 是仍標為 running 的歷史紀錄，可能來自中斷程序，**不是目前
正在執行的工作數**。此指令不做資料庫完整性修復。

roots 最多回傳 10000 個根目錄、合計 10000 條排除規則及 16 MiB 文字；超出時會
回報錯誤，不會截斷結果。status 計數不受根目錄清單上限影響，但大型索引可能需要
較多查詢時間。缺少資料庫回報 4，不會建立空資料庫。

### 12. 執行可重現的搜尋 benchmark

先完成 Release 建置，再執行開發工具（不是 fileweaver 子指令）：

```powershell
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --files 1000 --repetitions 5 --warmups 1 --storage-kind unknown --output build/benchmarks/search-run.json
```

加上 `--core-runner` 可在同一份合成索引量測 C++ 應用層搜尋，並另外記錄實際 SQLite
版本、量測工具 SHA-256、原始樣本與 p50／p95：

```powershell
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --core-runner build/release/search_core_benchmark.exe --build-info build/release/benchmark-build-info-Release.json --files 1000 --repetitions 5 --warmups 1 --storage-kind unknown --output build/benchmarks/search-with-core.json
```

核心計時包含資料庫開啟、查詢準備、SQL 執行與結果建立；不包含程序啟動、參數建立、
結果核對、結果銷毀或 JSON 輸出。每次仍會在計時後核對完整檔名、路徑、大小與順序，
錯誤就停止且不寫成功報告。核心工具必須與 CLI 位於同一建置目錄；報告版本為 2。
原有三種方法仍會執行，計時範圍不同，不應直接換算成演算法加速比。

若要觀測實際 CLI 程序的記憶體，可另加 `--cli-memory`：

```powershell
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --core-runner build/release/search_core_benchmark.exe --cli-memory --build-info build/release/benchmark-build-info-Release.json --files 1000 --repetitions 5 --warmups 1 --storage-kind unknown --output build/benchmarks/search-with-cli-memory.json
```

這會在計時後額外啟動六次搜尋，逐次核對結果；Windows 於每個子程序結束後透過保留的
查詢控制代碼取得工作集峰值，沒有修改 CLI 或對系統其他程序做監控。報告版本為 3，
`cli_memory` 包含各查詢的數值、錯誤狀態與來源，另以 `cli_memory_observer_sha256`
記錄觀測工具 SHA-256。六次都成功時，
`environment.cli_peak_rss_bytes` 才填入其中最大值；部分失敗時該摘要維持 null，
成功取得的最大值另放 `cli_memory.observed_max_bytes`，並標為 partial。
其他平台或 API 取值失敗會保留 null 及原因。不加旗標就不執行額外觀測。
這些是獨立觀測程序的峰值，不是計時樣本本身的記憶體用量，也不是所有查詢的保證上限。

啟用核心工具時，報告的 `core_memory` 另列量測前後的程序記憶體峰值、位元組單位、
來源與可用狀態；`core_peak_rss_bytes` 保存工作完成時取得的峰值。Windows 使用程序
工作集峰值，Linux 實作讀取程序 RSS 峰值（已通過 Linux 合成 smoke 測試）；其他平台、API 失敗
或舊工具未提供欄位時保留 null 並附狀態。沒有 `--core-runner` 時核心記憶體欄位為 null。
這是程序生命週期截至搜尋與驗證完成的高水位，包含啟動、參數、結果核對與量測資料；
不包含之後的報告序列化或結束階段，不能把前後差值當成單次搜尋配置量或記憶體洩漏。

工具只在新的暫存資料夾建立合成的零位元組 .txt 檔案，使用真正的 init／index／search
流程，逐次驗證搜尋的檔名、路徑、大小及固定順序。結束後清理自己建立的暫存資料。
不接受真實來源根目錄，報告也不保存暫存路徑或使用者／電腦名稱。既有報告不會被覆寫。

`--files` 接受 1–100000，`--repetitions` 接受 2–200，`--warmups` 接受 1–10。
`--dataset` 預設 `flat-ascii`，維持單層目錄與不重複 ASCII 檔名。可選
`nested-unicode` 產生含中文、日文、重音字元與 emoji 的根目錄、資料庫與檔名，
並在三個分支中放置同名檔案；共有兩層子目錄，最多 27 個資料夾（不含根目錄）。
報告版本為 4，列出資料集、目錄數及不同檔名數。此資料集只使用合法 Unicode，
不會改變對不完整 Windows 原生字元編碼的處理。

```powershell
python benchmarks/search/benchmark_search.py --cli build/release/fileweaver.exe --core-runner build/release/search_core_benchmark.exe --cli-memory --dataset nested-unicode --build-info build/release/benchmark-build-info-Release.json --files 1000 --repetitions 5 --warmups 1 --storage-kind unknown --output build/benchmarks/search-nested-unicode.json
```

每三個檔案共用一個檔名，因此 100 筆限制會截在同名群組內。基準會比較完整路徑順序，
確認截斷選中正確項目；巢狀目錄使用明確標示的 `python_recursive_directory_scan`
遞迴基準，原本單層資料集仍使用 `python_flat_directory_scan`。不同資料集的查詢分布
與計時範圍不同，不應直接換算加速比。核心工具需使用支援新資料集的同一建置版本。

`--storage-kind` 可填 unknown／ssd／hdd；不確定請保留 unknown。可用 `--storage-model`
記錄已確認的型號，工具不會自動推測儲存媒體。build-info 由 CMake 產生，必須與執行檔
在同一建置目錄。Debug 僅可用 `--smoke` 做正確性檢查，不能當成效能驗收。

報告包含編譯器／模式、執行檔與工具 SHA-256、CPU、RAM、OS、可取得的 Python
驅動程序 peak RSS、資料形狀、查詢分布、每筆原始計時與 nearest-rank p50／p95。
無法取得的欄位保持 null／unknown；CLI 記憶體只在 `--cli-memory` 啟用時觀測。核心工具的峰值
與 Python 驅動程序的峰值分開記錄，不能拿整體程序峰值代表單次搜尋的額外用量。

三種方法分別是 CLI 搜尋＋JSON＋程序啟動、Python／SQLite 強制全表掃描、Python
目錄檔名掃描（依資料集採單層或遞迴）。它們的執行範圍與 SQLite 版本可能不同，**不能直接當成 C++ 核心
演算法加速比**。首次觀察發生在建立檔案及索引之後，沒有清除 OS 快取；報告分開保存
首次、暖機與量測樣本，不會將首次稱為冷快取。可選核心工具在其他方法之後執行，
首次呼叫也已受快取影響。受控冷快取、跨平台 CLI 記憶體驗證與最佳化搜尋索引的完整比較
仍待補齊，因此不代表 SPEC 的 p95 目標已達成。

本機已完成一次 Release／SSD／10 萬個合成檔案量測，126 筆樣本皆通過結果驗證。
暖機後 CLI 端對端 p95：精確搜尋 19.5988 ms、子字串搜尋 54.0007 ms。
完整環境、方法範圍、原始報告位置與限制見 [10 萬檔案基準紀錄](docs/performance/search-baseline-100k.md)。

另一次含核心工具的 10 萬檔案量測，168 筆樣本全部正確；已暖 OS 快取的 C++ 應用層
搜尋 p95 為精確 1.1127 ms、子字串 35.0718 ms。這是新增量測方式，不代表搜尋變快；
完整範圍、環境及限制見 [核心搜尋基準紀錄](docs/performance/core-search-baseline-100k.md)。
開發驗證指令與結果見 [核心量測驗證](docs/core-benchmark-verification.md)。

補上記憶體欄位後的 Windows／10 萬合成檔案量測，168 筆樣本全部正確；原生量測程序
截至工作完成的工作集峰值為 11718656 bytes（約 11.18 MiB），不代表單次搜尋額外配置量
或整個產品的用量上限。環境、前後峰值、原始報告與限制見
[原生記憶體基準](docs/performance/native-memory-baseline-100k.md)；完整檢查見
[記憶體量測驗證](docs/memory-benchmark-verification.md)。

另一次啟用 `--cli-memory` 的 10 萬檔案量測，168 筆計時樣本與額外六次 CLI 搜尋全部
通過結果驗證，六次記憶體觀測皆可用；其中最大工作集峰值為 11444224 bytes（約
10.91 MiB）。這是該組獨立觀測程序的結果，不是產品用量上限。完整逐筆數值、環境與
限制見 [CLI 記憶體基準](docs/performance/cli-memory-baseline-100k.md)，檢查指令與結果見
[CLI 記憶體驗證](docs/cli-memory-verification.md)。

`nested-unicode` 的 10 萬檔案量測涵蓋 27 個子目錄與 33334 種不同檔名；168 筆計時
樣本與額外六次 CLI 觀測全部通過結果核對，包括同名群組內的路徑排序與 100 筆截斷。
完整原始數值與限制見 [多語言巢狀基準](docs/performance/nested-unicode-baseline-100k.md)，
驗證指令與邊界測試見 [巢狀資料集驗證](docs/nested-benchmark-verification.md)。

### 13. 驗證組合篩選與離線索引

開發用正確性工具會在兩個新的多語言暫存根目錄，產生不同大小、副檔名與固定修改時間
的合成檔案，驗證組合篩選、排序與限制筆數；接著把自己產生的根目錄移到同一暫存
工作區內的離線位置，再核對相同索引結果。不接受真實來源根目錄，也不讀取使用者檔案。

```powershell
python benchmarks/search/verify_filters.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --per-root 72 --output build/validation/filter-matrix.json
```

每個根目錄可產生 1–1024 個檔案，預設各 72 個；大小為 0／1／127／4096 bytes，
副檔名包含 txt／TXT／md／tar.gz／無副檔名／Unicode。每階段 120 個有效查詢，
線上及離線共 240 次，另有 14 個無效參數情境。報告記錄完整案例設定、核對結果與
工具／執行檔識別，不保存暫存路徑；既有報告不覆寫。Debug 也能執行。這是代表性
正確性矩陣，不是完整笛卡兒組合測試，也沒有計時或效能驗收結論。

此工具已找出並驗證修復 CLI 明確空文字參數被當成未設定的缺陷。Debug／Release
各 8 組測試通過；兩個版本也各完成預設資料集的 240 次有效查詢及 14 個無效情境。
完整指令、修正前失敗證據、報告位置與限制見 [組合篩選驗證](docs/filter-matrix-verification.md)。

### 14. 驗證增量重掃與檔案識別

開發用工具在兩個新的合成根目錄，依序測試初次索引、檔案已變動但尚未重掃、明確
重掃、未變動時 refresh，以及原路徑重新出現後 refresh。它沿用組合篩選矩陣，再加入
針對新增／改名／大小變動的查詢，每個階段 124 個案例、五階段共 620 次搜尋。

```powershell
python benchmarks/search/verify_incremental.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --per-root 24 --output build/validation/incremental.json
```

每個根目錄接受 3–1024 個檔案，預設各 24 個。工具只會修改、改名或移除自己產生的
暫存檔案，先檢查所有操作路徑位於該工作區並拒絕目的地衝突；不接受真實來源目錄。
除了完整篩選結果與排序，也會核對掃描統計、Missing 紀錄、未變動更新，以及檔案 ID：
既有路徑修改後保留 ID，改名視為新路徑，原先 Missing 的路徑重新出現後恢復原 ID。
這不代表跨路徑追蹤同一實體檔案，也不是產品的重新命名或復原功能。

報告只記錄合成案例、數量、識別檢查及程式雜湊，不保存暫存路徑；既有報告不覆寫。
此工具只驗證正確性，沒有計時或效能結論，也不代表已驗證並行競態、中斷或內容雜湊。

Debug／Release 各 9 組測試已通過。預設 Release 合成資料已完成 620 次搜尋、8 次掃描、
5 個階段的狀態與 ID 核對；完整指令、逐階段統計及限制見
[增量一致性驗證](docs/incremental-filter-verification.md)。

### 15. 驗證失敗保留、部分更新與復原

此開發工具只在新建的暫存資料庫注入可重現的寫入失敗，並暫時限制合成根目錄的
掃描深度，核對失敗時舊索引與 ID 是否保留，修復後再重掃；不接受既有資料庫或真實根目錄。

```powershell
python benchmarks/search/verify_failure_recovery.py --cli build/release/fileweaver.exe --build-info build/release/benchmark-build-info-Release.json --per-root 24 --output build/validation/failure-recovery.json
```

每個根目錄接受 3–1024 個合成檔案，預設各 24 個。六階段各 124 個篩選案例，共
744 次搜尋，另核對根目錄狀態、Missing、掃描統計、檔案 ID 與測試故障物件清除。
寫入故障允許第一筆異動完成、第二筆才中止，驗證整個根目錄的索引交易回滾；
深度限制則驗證已觀察到的部分資料也不會提交。

`refresh` 採逐根目錄交易：某個根目錄失敗時保留它的舊索引，其他正常根目錄仍可更新；
不是整批一起回滾，也不會還原來源檔案的實際變動。失敗與不完整掃描會回報非零退出碼。
此工具只驗證合成儲存寫入失敗及深度限制，不代表已驗證真實磁碟損壞、磁碟滿、權限不足、
取消或競態。報告不保存暫存路徑、既有報告不覆寫，且沒有計時或效能結論。

Debug／Release 各 10 組測試已通過；預設 Release 合成資料完成 744 次搜尋與三種失敗
路徑的保留／復原核對。完整指令、逐階段狀態與限制見
[失敗保留與復原驗證](docs/failure-recovery-verification.md)。

### 16. 驗證 C++ API 的合作式取消檢查點

目前 C++ 掃描／索引／refresh API 可接收 `std::stop_token`。新增的固定檢查點測試
用 metadata 回呼及注入時鐘要求停止，不使用睡眠或時間競賽：

```powershell
.\build\debug\storage_tests.exe '[cooperative-cancellation]' --reporter compact
```

測試核對掃描停止後不再交付 metadata、索引取消時保留舊結果與 ID、取消狀態與
掃描歷史、未執行根目錄、儲存設定保留，以及後續重掃能恢復。若在第一個根目錄
已完成掃描後才要求取消，該根目錄仍可能提交，後續根目錄不再執行；不是整批回滾。

這是既有 C++ API 的測試，沒有新增 CLI 取消選項或 Ctrl+C 處理，也不代表強制終止程序、
SQL 執行中或已寫入部分資料時的索引取消都已驗證。CLI 的優雅 Ctrl+C 取消仍未實作。
所有測試只使用自己產生的暫存檔案。

新增取消測試為 3 個案例、317 個斷言；Debug／Release 各 10 組 CTest 通過，C++ 全套
為 57 個案例、700 個斷言。完整命令、取消界線與未涵蓋情境見
[合作式取消驗證](docs/cooperative-cancellation-verification.md)。

### 退出碼與常見問題

| 退出碼 | 意義與處理方式                                                           |
| ------ | ------------------------------------------------------------------------ |
| 0      | 成功，或成功顯示說明                                                     |
| 1      | 其他執行錯誤；查看 stderr 診斷訊息                                       |
| 2      | 參數不正確，例如缺少 `--database`、未知命令或無效篩選值 |
| 3      | 無法開啟資料庫；確認父資料夾存在、路徑正確且有寫入權限                   |
| 4 | 索引尚未初始化；先執行 init --database |
| 5      | schema、資料庫損毀或其他初始化錯誤；依診斷訊息處理，勿直接刪除重要資料庫 |

PowerShell 可用 `$LASTEXITCODE` 查看上一個程式的退出碼。
詳細錯誤訊息送至 stderr；啟用 --json 時 stdout 另外提供一份結構化錯誤文件。

## 安全與目前限制

程式僅掃描你明確指定的來源目錄之中繼資料，不讀取檔案內容或修改來源檔案；
沒有遙測、剪貼簿收集或執行時網路存取。索引會保存完整路徑、檔名與時間，
請自行保護資料庫及附屬檔案的存取權限。
`init` 會建立或驗證指定的本機 SQLite 資料庫，使用交易式 schema 初始化、
WAL 與外鍵設定，拒絕不支援的版本。

本機 Windows GCC 13.2 的 Debug／Release 建置、CTest、format 與 lint 已通過。
GitHub Actions 的 Windows/MSVC、Ubuntu/GCC Debug／Release，以及 Clang 的
format／lint／ASan／UBSan 已全部通過。[成功 CI](https://github.com/Rex60309/FileWeaver/actions/runs/42564387945)
與各提交的失敗、修正及本機指令見 [Phase 1 驗收紀錄](docs/phase1-acceptance.md)。
目前沒有正式發行版本；效能數據限單機合成基準，不代表所有硬體或整份產品 SPEC 均已驗收。詳見 [功能狀態](docs/feature-status.md) 與
[驗證紀錄](docs/verification.md)。完整規劃請見 [產品規格](PRODUCT_SPEC.md)。
本次索引與回歸測試的命令及結果見 [索引驗證紀錄](docs/index-writer-verification.md)。
檔名搜尋與 CLI 輸出的命令及結果見 [搜尋驗證紀錄](docs/search-verification.md)。
副檔名、大小與根目錄篩選的驗證結果見 [篩選驗證紀錄](docs/filter-verification.md)。
UTC 修改時間解析與篩選的驗證結果見 [時間篩選驗證紀錄](docs/time-filter-verification.md)。
搜尋排序的驗證結果見 [排序驗證紀錄](docs/sort-verification.md)。
JSON 欄位、錯誤輸出與嚴格解析測試見 [JSON 驗證紀錄](docs/json-verification.md)。
schema v3 升級、設定保存與 refresh 的結果見 [更新驗證紀錄](docs/refresh-verification.md)。
roots／status 唯讀檢查的結果見 [檢查驗證紀錄](docs/inspection-verification.md)。
開發用 benchmark 工具與煙霧測試見 [benchmark 驗證紀錄](docs/benchmark-verification.md)。

## Build and verify

Requirements: CMake 3.25+, Ninja, a working C++20 compiler, clang-format, clang-tidy,
Python 3.9+ with its standard-library json/sqlite3 modules for development tests,
and bootstrapped vcpkg at the baseline in vcpkg.json. Set `VCPKG_ROOT` to that
checkout before configuring. CMake installs the locked dependencies through its
toolchain (this development step requires network access). Run from a
compiler-enabled terminal:

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
```

On supported Linux/macOS compilers, configure, build, and test the `sanitizers`
preset. Combined ASan/UBSan is explicitly rejected on Windows.

After building, run `build/debug/fileweaver --help` (Windows: `fileweaver.exe`).
Invalid arguments return exit code 2 and diagnostics go to stderr.

Initialize only a database path you explicitly choose:

```text
fileweaver init --database <existing-parent-directory>/index.sqlite
```

This creates SQLite schema v3 with settings, roots, files, scans and saved root policy tables, WAL and foreign
keys. Repeated initialization preserves settings; unknown schemas are rejected.
Open/permission failures return 3 and schema failures return 5. Initialization
does not scan files. All commands/help support --json with exactly one stdout
document and diagnostics on stderr. Python is not required to run the product.

Schema v1/v2 is upgraded transactionally to v3 with metadata/settings preserved. Failed
migrations roll back; no automatic downgrade is provided. The `index`
command writes metadata and supports conservative re-scans. `search` supports
exact binary filename matching and ASCII-insensitive literal substring/prefix matching with
deterministic ordering and bounded results. Advanced search remains planned.
Refresh reuses saved policies. Migrated roots require explicit re-indexing to
confirm exclusions before refresh; no default policy is guessed for legacy roots.

On the inspected Windows environment, select the working compiler explicitly:
`cmake --preset debug -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe`
(use the same override for release, plus vcpkg's `x64-mingw-dynamic` target and
host triplets). Windows GCC 13.2.0 Debug/Release and all 11 CTest entries have
passed. Ubuntu WSL2 GCC 13.3.0 Debug/Release and ASan/UBSan also have executed
passing evidence; current-revision acceptance is tracked in
[the Phase 1 ledger](docs/phase1-acceptance.md). Hosted CI results are recorded
there separately from local checks. See docs/verification.md for command details.
For matching analysis-tool versions, install
`python -m pip install --target build/tools/python -r tools/requirements-analysis.txt`.
These developer tools are separate from product runtime dependencies.
The sanitizer test preset enables leak detection and stops on ASan/UBSan errors;
it is intended for the supported Linux/macOS compiler configurations.

Indexing reads metadata only from explicitly selected roots. There is no source-file
mutation, content collection, telemetry, or runtime network activity. Storage writes
are confined to the explicitly selected database and SQLite sidecar files. Tests
use generated temporary fixtures. A scoped synthetic search baseline is recorded
in docs/performance; no core-latency target compliance or screenshots are claimed.
See PRODUCT_SPEC.md for the roadmap and docs/feature-status.md for actual status.
