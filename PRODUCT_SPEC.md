# FileWeaver — Complete Product & Technical Specification

**Version:** 2.0  
**Status:** Proposed Specification  
**Product:** FileWeaver — Personal File Command Center  
**Primary Language:** C++20  
**Primary Platform:** Windows 10/11 x64  
**Secondary Platforms:** Linux / macOS（核心跨平台，功能依平台逐步支援）  
**Application Type:** Desktop GUI + CLI + Background Indexing Service  
**Primary Goal:** Build a fast, privacy-first, local file search and productivity application.

---

# 1. Product Overview

## 1.1 Product Vision

FileWeaver 是一款以 C++20 開發的高效能本機檔案搜尋、整理、分析與開發工作流程管理工具。

使用者可以透過全域快捷鍵快速搜尋檔案、預覽內容、管理重複檔案、分析磁碟空間、整理下載資料夾、搜尋剪貼簿歷史紀錄、管理開發專案，以及建立檔案版本備份。

產品採用 **Local-First、Privacy-First、Performance-Oriented** 設計。

核心原則：

1. 預設不將任何個人檔案、搜尋紀錄或索引上傳雲端。
2. 搜尋功能必須在本機離線運作。
3. 大型檔案集合下仍維持低延遲與可控記憶體使用量。
4. 對使用者檔案的修改必須安全、可預覽，並在可行範圍內可復原。
5. 核心功能不依賴 AI 模型。
6. AI Semantic Search 必須明確啟用，並採用本機推論。
7. CLI 與 GUI 共用相同的核心服務，不重複實作業務邏輯。

## 1.2 Target Users

### General Users
- 經常找不到文件、圖片、下載檔案。
- 希望整理重複檔案與磁碟空間。
- 希望透過快捷鍵快速開啟檔案與應用程式。

### Students
- 管理課堂作業、PDF、簡報、論文及程式碼。
- 快速尋找不同學期的資料。
- 管理大量課程資料夾與專案。

### Developers
- 快速開啟 Git Repository。
- 搜尋原始碼、設定檔與開發文件。
- 啟動 VS Code、Terminal、Build Commands。
- 搜尋 Git 專案與常用工作目錄。

---

# 2. Functional Scope

## 2.1 Feature Overview

| ID | Feature | Priority | Phase |
|---|---|---|---|
| F01 | Local File Indexing | P0 | Core |
| F02 | Fast Filename Search | P0 | Core |
| F03 | Advanced Search Filters | P0 | Core |
| F04 | Incremental Indexing | P0 | Core |
| F05 | Global Hotkey Launcher | P0 | Desktop |
| F06 | Fuzzy Search & Ranking | P0 | Desktop |
| F07 | Real-Time File Watching | P0 | Desktop |
| F08 | File Preview | P1 | Desktop |
| F09 | Disk Space Visualization | P1 | Desktop |
| F10 | Duplicate File Detection & Management | P1 | Management |
| F11 | Downloads Organizer | P1 | Management |
| F12 | Batch Rename | P1 | Management |
| F13 | Clipboard History Search | P1 | Productivity |
| F14 | Developer Project Launcher | P1 | Productivity |
| F15 | Recent File Activity Timeline | P1 | Productivity |
| F16 | AI Semantic File Search | P2 | Advanced |
| F17 | Versioned Backup & Restore | P2 | Advanced |
| F18 | CLI & JSON Output | P0 | Core |
| F19 | Benchmark & Diagnostics | P0 | Core |

P0 = 核心功能，P1 = 重要功能，P2 = 進階擴充。

**Phase 是交付階段，不代表所有功能必須同時完成。**

---

# 3. Core File Indexing Engine

## 3.1 Purpose

在本機建立可持久化的檔案 metadata 索引，讓後續搜尋不需要每次重新掃描檔案系統。

## 3.2 Indexed Metadata

每個檔案至少儲存：

- File ID
- Root ID
- Absolute Path
- Normalized Path
- Filename
- Extension
- Size in Bytes
- Creation Time（平台支援時）
- Last Modified Time
- Last Indexed Time
- File Type
- Presence Status
- Optional Content Hash
- Optional File Identity（平台相關）

預設不儲存完整檔案內容。

## 3.3 Indexing Requirements

- 支援多個指定 Root Directory。
- 使用 `std::filesystem` 遞迴掃描。
- 處理 Unicode Paths。
- 支援 Exclusion Rules。
- 不預設跟隨 Symlink、Junction 或 Reparse Point。
- Permission Denied 不得造成整次索引失敗。
- 檔案掃描中消失時安全略過。
- 支援 Cancellation。
- 大量資料使用 Bounded Queue，避免無限制累積記憶體。
- Database Writes 使用受控 Writer。
- 掃描狀態必須可追蹤。

## 3.4 Incremental Indexing

重新掃描時：

1. 取得目前檔案 metadata。
2. 比對既有索引。
3. 未變更的檔案跳過不必要的更新。
4. 已變更檔案更新 metadata。
5. 不存在的檔案標記 Missing。
6. 失效的 Hash Cache 必須清除或重新驗證。

檔案大小與修改時間可用作快速變更判斷，但不得視為內容必然相同的證明。

## 3.5 Real-Time File Watching

Windows 使用 `ReadDirectoryChangesW` 或適當的 Windows File Notification API。

Linux 可使用 `inotify`。

macOS 可使用 FSEvents。

要求：

- 監控使用者指定的 Root。
- 合併短時間內重複事件。
- 支援檔案建立、刪除、修改與重新命名。
- 處理事件佇列 Overflow。
- 監控中斷後執行 Reconciliation Scan。
- 避免重複事件造成索引不一致。
- 顯示最後同步時間。

即時監控不得取代完整掃描與一致性驗證。

---

# 4. Fast Search Engine

## 4.1 Search Modes

支援：

1. Exact Filename Search
2. Case-Insensitive Substring Search
3. Prefix Search
4. Glob Search
5. Fuzzy Search
6. Path Search
7. Advanced Filter Search

例如：

```text
report
final_report.pdf
*.cpp
image processing
img proc fin
```

## 4.2 Search Index Architecture

Baseline 使用 SQLite。

針對大量資料，評估以下方案：

- B-tree Index
- Prefix Index
- Trigram Inverted Index
- Candidate Filtering
- In-Memory Cache

任意 Substring Search 不得假設普通 SQLite B-tree 索引一定能有效加速。

進階搜尋索引須與 SQLite 中的檔案狀態保持一致。

## 4.3 Fuzzy Matching

支援：

- Typo Tolerance
- Token Matching
- Prefix Matching
- Subsequence Matching
- Path Component Matching

例如：

```text
Query: img proc fin

Result:
image_processing_final_report.pdf
```

可評估的演算法：

- Levenshtein Distance
- Damerau-Levenshtein Distance
- Trigram Similarity
- Token-Based Scoring

不得在每次輸入時對全部檔案執行高成本編輯距離計算；必須先產生候選集合。

## 4.4 Ranking

排序因素可包含：

- Filename Exact Match
- Prefix Match
- Token Match
- Fuzzy Similarity
- Path Match
- Recently Modified
- Pinned Files
- User Favorites
- Explicitly Recorded Open Frequency

Ranking 權重必須可設定及測試。

使用者操作紀錄須可清除，且預設不得上傳。

## 4.5 Search Filters

支援：

```text
--ext
--min-size
--max-size
--modified-after
--modified-before
--root
--limit
--sort
```

GUI 提供對應的可視化篩選條件。

---

# 5. Global Hotkey Launcher

## 5.1 Purpose

讓使用者不必開啟完整 FileWeaver 主視窗，也能立即搜尋及開啟檔案。

## 5.2 Main Workflow

1. 使用者按下設定的快捷鍵。
2. 顯示浮動搜尋視窗。
3. 游標自動聚焦搜尋欄。
4. 使用者輸入關鍵字。
5. 顯示即時搜尋結果。
6. 使用方向鍵選取。
7. 按 Enter 開啟。
8. 按 Escape 關閉。

預設快捷鍵可設定為 `Alt + Space`，但必須偵測快捷鍵衝突，允許使用者修改。

## 5.3 Result Types

- Files
- Directories
- Applications
- Developer Projects
- Pinned Items

## 5.4 Keyboard Controls

| Key | Action |
|---|---|
| Enter | Open selected item |
| Up / Down | Navigate |
| Escape | Close launcher |
| Ctrl + C | Copy selected path |
| Ctrl + Enter | Open containing folder |
| Tab | Switch result category |

快捷鍵需避免與作業系統既有操作衝突。

## 5.5 Technical Requirements

- 使用 Qt Widgets 或 Qt Quick。
- Windows 使用 `RegisterHotKey`。
- GUI 不得阻塞搜尋執行緒。
- 搜尋結果支援取消舊查詢。
- 以 Debouncing 減少不必要的查詢。
- 開啟檔案前驗證目標仍存在。

---

# 6. Quick File Preview

## 6.1 Supported Formats

第一階段：

- TXT
- Markdown
- JSON
- XML
- CSV
- Source Code
- PNG
- JPEG

第二階段：

- PDF
- GIF
- Additional Image Formats

## 6.2 Requirements

- 不必開啟外部程式即可預覽。
- 支援文字 Syntax Highlighting。
- 大型文字檔只讀取必要區段。
- 圖片使用縮圖快取。
- PDF 使用受維護的解析與渲染元件。
- 不執行檔案內嵌腳本、巨集或程式碼。
- 對不受信任的檔案設定大小與資源限制。
- 無法預覽時顯示原因並提供外部開啟選項。

---

# 7. Disk Space Analyzer

## 7.1 Features

- Directory Size Breakdown
- File Type Distribution
- Largest Files
- Old Files
- Storage Treemap
- Sort by Size
- Filter by Root
- Export Reports

## 7.2 Treemap

使用 Rectangle Packing 或 Squarified Treemap。

每個區塊代表檔案或資料夾。

面積對應檔案大小。

支援：

- Zoom Into Directory
- Breadcrumb Navigation
- Hover Details
- Open Containing Folder
- Filter by File Type

## 7.3 Accuracy

報表必須區分：

- Indexed Logical File Size
- Actual Disk Allocation（平台支援時）
- Unreadable Directories
- Missing Files
- Scan Timestamp

不得將 Logical File Size 直接宣稱為可釋放的實際磁碟空間。

---

# 8. Duplicate File Management

## 8.1 Detection Algorithm

1. 依檔案大小分組。
2. 排除只有一個檔案的群組。
3. 對候選檔案計算 Streaming SHA-256。
4. 比對 Size + SHA-256。
5. 顯示候選重複檔案。
6. 修改檔案前重新驗證檔案狀態。

## 8.2 User Interface

顯示：

- Duplicate Group
- Filename
- File Size
- Full Path
- Last Modified
- Content Hash
- Potential Reclaimable Space

## 8.3 Safe Management

使用者可以：

- Preview
- Select Files
- Open Containing Folder
- Export Report
- Move Selected Files to Recycle Bin（後續版本）

預設不得自動刪除。

## 8.4 Safety Requirements

- 刪除或移動前顯示確認。
- 不得刪除群組中的最後一份保留副本。
- 支援 Dry Run。
- 重要操作記錄 Transaction Journal。
- 優先使用作業系統 Recycle Bin。
- 永久刪除不列為 MVP 功能。
- Recycle Bin 的復原能力依平台而異，不保證所有情況都可 Undo。

---

# 9. Smart Downloads Organizer

## 9.1 Purpose

自動整理 Downloads 等指定資料夾。

## 9.2 Rule Examples

```text
*.pdf -> Documents/PDF
*.zip -> Archives
*.jpg -> Images
*.png -> Images
*.exe -> Installers
```

## 9.3 Rule Engine

條件：

- Extension
- Filename Pattern
- File Size
- Modification Date
- Source Directory

動作：

- Move
- Copy
- Rename
- Add Tag（FileWeaver 內部標籤）

## 9.4 Rule Priority

- 規則依 Priority 執行。
- 衝突規則必須有明確解決方式。
- 不得在未設定時覆蓋既有檔案。
- 支援 Dry Run。
- 支援手動執行與監控觸發。
- 正在下載或仍被寫入的檔案應延後處理。
- 自動整理預設關閉，需使用者啟用。

## 9.5 Undo

移動與重新命名需記錄：

- Original Path
- Destination Path
- Operation Time
- Operation Status
- File Identity / Metadata

Undo 前必須確認原始與目標位置未產生衝突。

---

# 10. Batch Rename

## 10.1 Features

- Add Prefix
- Add Suffix
- Sequential Numbering
- Replace Text
- Case Conversion
- Date-Based Naming
- Regex Replacement（進階功能）

## 10.2 Preview

執行前顯示：

| Original | New Name | Status |
|---|---|---|
| IMG001.jpg | trip_001.jpg | Ready |
| IMG002.jpg | trip_002.jpg | Ready |

## 10.3 Safety

- 偵測同名衝突。
- 偵測 Windows 保留名稱。
- 處理大小寫不敏感檔案系統。
- 避免重新命名順序造成覆蓋。
- 支援 Dry Run。
- 使用暫存名稱處理循環命名衝突。
- 操作紀錄與 Undo。
- 跨磁碟移動不視為單純 Rename。

---

# 11. Clipboard History Manager

## 11.1 Features

- Text Clipboard History
- Search Clipboard Items
- Pin Favorite Snippets
- Copy Again
- Delete Individual Items
- Clear All History
- Configurable Retention

## 11.2 Privacy Requirements

剪貼簿可能包含密碼、驗證碼、API Keys 或個人資料。

因此：

- 預設關閉歷史紀錄收集。
- 必須經使用者明確啟用。
- 提供 Pause Monitoring。
- 支援排除指定應用程式（平台可行時）。
- 支援敏感內容偵測與不儲存規則。
- 不記錄來自明確標記為敏感的剪貼簿資料（平台支援時）。
- 儲存資料需使用 OS 保護機制或適當加密。
- 支援自動過期與立即清除。
- 不同步到雲端。

敏感內容偵測只能降低風險，不能保證識別所有密碼或秘密資訊。

---

# 12. Developer Project Launcher

## 12.1 Purpose

提供開發者快速切換專案的入口。

## 12.2 Project Discovery

搜尋使用者指定目錄中的：

- `.git`
- `CMakeLists.txt`
- `package.json`
- `pyproject.toml`
- `Cargo.toml`

偵測結果僅作為候選專案，不應假設每個資料夾只有一種專案類型。

## 12.3 Project Metadata

- Project Name
- Root Path
- Project Type
- Git Branch
- Last Modified
- Favorite Status
- Configured Commands

## 12.4 Actions

- Open in VS Code
- Open in Terminal
- Open Folder
- View Git Status
- Run Configured Build Command
- Run Configured Test Command

## 12.5 Command Execution Safety

- 不執行搜尋結果中任意文字作為 Shell Command。
- 所有執行命令需由使用者設定或明確核准。
- 顯示實際 Command、Arguments、Working Directory。
- 優先使用 Process API，而不是字串拼接 Shell。
- 支援執行取消與 Timeout。
- 保留必要的 Exit Code 與執行結果。
- 不自動執行不受信任 Repository 的腳本。

---

# 13. Recent File Activity Timeline

## 13.1 Features

- Recently Modified Files
- Recently Indexed Files
- File Creation Events
- File Rename Events
- File Deletion Events
- Recently Opened Through FileWeaver

## 13.2 Timeline

```text
Today
  10:30 Modified report.pdf
  10:15 Created main.cpp

Yesterday
  21:00 Renamed project_notes.md
```

## 13.3 Accuracy

必須區分：

- 檔案系統監控觀察到的事件。
- 從 metadata 推得的修改時間。
- FileWeaver 自己記錄的開啟操作。

不得將檔案修改時間直接宣稱為使用者開啟時間。

## 13.4 Retention

- 可設定保存天數。
- 可停用活動記錄。
- 可清除歷史。
- 不上傳使用者活動資料。

---

# 14. AI Semantic File Search

## 14.1 Purpose

讓使用者用自然語言搜尋文件內容。

例如：

```text
上學期影像處理的期末報告
```

## 14.2 Architecture

```text
User Query
    |
Query Preprocessing
    |
Embedding Model
    |
Vector Search
    |
Candidate Retrieval
    |
Metadata Filtering
    |
Ranking
    |
Search Results
```

## 14.3 Content Indexing

與原本 metadata-only 索引分開管理。

支援明確選定的文件類型，例如：

- TXT
- Markdown
- PDF（文字可抽取時）
- Source Code

## 14.4 Technical Options

- Local Embedding Model
- ONNX Runtime
- Vector Similarity Search
- Optional HNSW Index
- SQLite Metadata Mapping

可使用現有本機模型執行引擎，不要求自行實作 Transformer。

## 14.5 Privacy & Resource Management

- 預設關閉。
- 使用者明確選擇要索引內容的資料夾。
- 不使用雲端 API 作為預設路徑。
- 提供模型下載大小與資源需求資訊。
- 支援暫停索引。
- 支援刪除 Embedding Index。
- 不將文件內容寫入一般診斷日誌。
- 對模型輸入大小、文件解析時間與記憶體設定限制。
- 不將文件中的文字視為可執行指令。

---

# 15. Versioned Backup & Restore

## 15.1 Purpose

保護指定資料夾中的重要文件。

## 15.2 Features

- Manual Snapshot
- Scheduled Snapshot
- Incremental Backup
- File Version History
- Restore Previous Version
- Backup Verification
- Retention Policy

## 15.3 Storage Architecture

可採用：

- Content-Addressable Storage
- Chunk-Based Storage
- Cryptographic Hash
- Snapshot Manifest
- Deduplication

## 15.4 Restore

復原前：

1. 選擇 Snapshot。
2. 預覽檔案差異。
3. 驗證備份內容完整性。
4. 選擇 Restore Destination。
5. 顯示覆蓋風險。
6. 執行 Restore。
7. 驗證結果。

## 15.5 Reliability

- 使用暫存檔與安全提交策略。
- 防止部分寫入造成備份不可用。
- Snapshot Manifest 需要版本資訊。
- 定期驗證備份完整性。
- 備份失敗不得破壞先前可用的 Snapshot。
- 支援備份目的地容量不足的處理。
- 避免將備份目錄重新納入自身備份。
- 清楚說明開啟中檔案的一致性限制。

本機同一磁碟的備份不能取代異地備份。

---

# 16. Application Architecture

## 16.1 High-Level Architecture

```text
                 FileWeaver
                     |
          +----------+----------+
          |                     |
       Qt GUI                  CLI
          |                     |
          +----------+----------+
                     |
              Application API
                     |
        +------------+------------+
        |            |            |
     Search       File Ops     Productivity
     Service      Service      Service
        |            |            |
        +------------+------------+
                     |
               Core Services
                     |
   +-----------------+------------------+
   |                 |                  |
 Index Engine    Search Engine     Event Manager
   |                 |                  |
 File Scanner    Ranking Engine    File Watcher
   |                 |                  |
   +-----------------+------------------+
                     |
               Persistence
                     |
             SQLite / Indexes
                     |
           Platform Adapters
                     |
           Windows/Linux/macOS
```

Advanced Modules：

```text
AI Semantic Search
    |
Local Embedding Runtime
    |
Vector Index

Backup Manager
    |
Snapshot Engine
    |
Content Store
```

## 16.2 Module Boundaries

### Core
- Domain Models
- Configuration
- Error Handling
- Task Scheduling
- Cancellation
- Service Interfaces

### Filesystem
- Directory Scanner
- File Metadata
- Path Normalization
- File Watching
- Platform Abstraction

### Search
- Query Parser
- Search Index
- Candidate Retrieval
- Fuzzy Matcher
- Ranking
- Filter Engine

### Database
- SQLite Repository
- Schema Migration
- Transactions
- Index Persistence

### File Operations
- Duplicate Detection
- Rename Planner
- Move Planner
- Operation Journal
- Undo Manager

### Productivity
- Clipboard History
- Project Discovery
- Launcher
- Activity Timeline

### Advanced
- Semantic Search
- Embedding Runtime
- Backup Engine

### Presentation
- CLI
- Qt GUI
- JSON Output

---

# 17. Technology Stack

| Component | Technology |
|---|---|
| Language | C++20 |
| Build | CMake |
| Dependency Management | vcpkg |
| GUI | Qt 6 |
| Database | SQLite |
| CLI Parsing | CLI11 |
| Formatting | fmt |
| Logging | spdlog |
| Hashing | OpenSSL SHA-256 |
| Testing | Catch2 / GoogleTest |
| Benchmark | Google Benchmark |
| CI | GitHub Actions |
| Static Analysis | clang-tidy |
| Formatting | clang-format |
| Memory Diagnostics | ASan / UBSan |
| Optional AI | ONNX Runtime |

依賴版本須鎖定，並記錄相容性。

---

# 18. Data Model

## 18.1 Core Tables

### roots
- id
- normalized_path
- display_path
- added_at
- last_scan_at
- last_scan_status

### files
- id
- root_id
- normalized_path
- display_path
- name
- extension
- size_bytes
- created_at
- modified_at
- indexed_at
- is_present
- sha256
- hash_metadata_version

### scans
- id
- root_id
- started_at
- finished_at
- status
- scanned_count
- new_count
- updated_count
- missing_count
- skipped_count

### file_events
- id
- file_id
- event_type
- event_time
- event_source
- old_path
- new_path

### projects
- id
- root_path
- name
- project_type
- favorite
- last_opened_at

### organizer_rules
- id
- priority
- enabled
- conditions_json
- action_json

### operation_journal
- id
- operation_type
- source_path
- destination_path
- status
- started_at
- completed_at
- undo_metadata

### clipboard_items
- id
- encrypted_content
- content_type
- created_at
- expires_at
- pinned

### snapshots
- id
- source_root
- manifest_path
- created_at
- status

### snapshot_files
- snapshot_id
- relative_path
- content_reference
- size_bytes
- checksum

### settings
- key
- value

## 18.2 Database Requirements

- SQLite WAL Mode。
- Foreign Keys Enabled。
- Prepared Statements。
- Schema Versioning。
- Transactional Migrations。
- Single Writer 或受控寫入架構。
- 不可在任意 Worker Thread 共用未同步的 SQLite Connection。
- 需要支援 Index Rebuild。
- Migration Failure 必須 Rollback。

大型內容儲存與向量索引可使用獨立資料檔或儲存引擎，不要求全部放進 SQLite。

---

# 19. CLI Specification

保留原本 FileWeaver CLI，並擴充命令。

```text
fileweaver init
fileweaver index <root>
fileweaver refresh
fileweaver search <query>
fileweaver analyze
fileweaver duplicates
fileweaver status
fileweaver roots
fileweaver remove-root <root>
fileweaver clear

fileweaver projects list
fileweaver projects open <name>

fileweaver organize preview
fileweaver organize run

fileweaver rename preview
fileweaver rename apply

fileweaver backup create
fileweaver backup list
fileweaver backup restore

fileweaver benchmark search
fileweaver doctor
```

所有會修改使用者檔案的命令都必須提供預覽與明確確認。

CLI 支援：

- Human-Readable Output
- JSON Output
- Stable Exit Codes
- Clear Error Messages
- Non-Interactive Mode
- Script Integration

沿用原版 Exit Code：

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Runtime Failure |
| 2 | Invalid Arguments |
| 3 | Workspace / Permission Error |
| 4 | Index Not Initialized |
| 5 | Schema / Migration Error |

新增的操作類型需映射到一致的錯誤分類，避免任意擴充不相容 Exit Code。

---

# 20. GUI Specification

## 20.1 Main Navigation

```text
FileWeaver
|
+-- Quick Search
+-- Files
+-- Recent Activity
+-- Storage Analyzer
+-- Duplicates
+-- Downloads Organizer
+-- Batch Rename
+-- Clipboard History
+-- Developer Projects
+-- Semantic Search
+-- Backup & Restore
+-- Settings
```

## 20.2 Search Interface

包含：

- Search Bar
- Category Tabs
- Filter Panel
- Result List
- File Preview
- Sort Controls
- Status Indicator

## 20.3 Background Operation

耗時操作顯示：

- Progress
- Files Processed
- Elapsed Time
- Estimated Remaining Time（有可靠估計時）
- Cancel Button
- Error Summary

## 20.4 Accessibility

- Keyboard Navigation
- High-DPI Scaling
- Light/Dark Theme
- Adjustable Font Size
- Screen Reader Labels（Qt 支援範圍內）
- Unicode Filename Support

---

# 21. Performance Requirements

以下為**驗收目標，不是已實現的效能聲明**。

測試需記錄：

- CPU
- RAM
- SSD / HDD
- OS Version
- Compiler
- Build Configuration
- Dataset Size
- Query Distribution
- Warm/Cold Cache
- Test Repetitions

## 21.1 Search

在 SSD、Release Build、10 萬筆 metadata 的測試資料集上：

- Exact Search p95：目標低於 50 ms。
- Substring Search p95：目標低於 200 ms。
- Fuzzy Search p95：目標低於 200 ms。
- GUI 搜尋不得阻塞主執行緒。

更大資料集需量測：

- 100,000 files
- 500,000 files
- 1,000,000 files

不預先承諾所有硬體都能達標。

## 21.2 Indexing

量測：

- Initial Scan Throughput
- Incremental Scan Time
- Database Size
- Peak Memory
- CPU Usage
- File Watcher Update Delay

## 21.3 Duplicate Detection

量測：

- Hash Throughput
- Peak Memory
- Single-thread vs Multi-thread
- Cache Hit Rate
- Repeated Scan Time

## 21.4 Benchmark Baselines

至少比較：

1. Direct Filesystem Scan
2. SQLite Baseline Search
3. Optimized Search Index

若實作 Fuzzy Search，再比較不同候選篩選與 Ranking 策略。

所有 Benchmark 必須可重現，不得使用虛構數據。

---

# 22. Security & Privacy

## 22.1 Local-First

- 預設無 Telemetry。
- 不上傳檔案索引。
- 不上傳搜尋紀錄。
- AI 功能預設使用本機模型。
- 網路功能必須明確揭露與授權。

## 22.2 File Safety

- 所有破壞性操作都要確認。
- 提供 Dry Run。
- 支援 Operation Journal。
- 儘可能支援 Undo。
- 不得默默覆蓋既有檔案。
- 使用者必須能檢查操作結果。

## 22.3 Sensitive Data

- Clipboard History 預設關閉。
- Semantic Content Indexing 預設關閉。
- 日誌不得預設記錄敏感內容。
- 支援資料清除。
- 支援指定排除資料夾。
- 提供索引與快取資料位置說明。

## 22.4 Input Validation

- SQL Prepared Statements。
- Path Validation。
- Safe Process Execution。
- 防止 Path Traversal。
- 避免 Shell Injection。
- 處理檔案系統 TOCTOU Race。
- 檔案解析設定 Resource Limits。

---

# 23. Testing Strategy

## 23.1 Unit Tests

- Query Parsing
- Fuzzy Matching
- Ranking
- Path Normalization
- Size Parsing
- Date Parsing
- Glob Matching
- Rename Planning
- Rule Matching
- Hashing
- Backup Manifest Validation

## 23.2 Integration Tests

- SQLite Repository
- Schema Migration
- Incremental Indexing
- File Watcher Reconciliation
- Duplicate Detection
- Organizer Rules
- Operation Journal
- Backup / Restore

## 23.3 End-to-End Tests

至少覆蓋：

```text
Init
  -> Index
  -> Search
  -> Analyze
  -> Detect Duplicates
  -> Refresh
  -> Verify Results
```

GUI 需測試：

- Launcher Open/Close
- Search Navigation
- File Preview
- Operation Confirmation
- Cancellation
- Settings Persistence

## 23.4 Edge Cases

- Empty Directory
- Unicode Path
- Long Path
- Permission Denied
- Missing File
- Symlink Loop
- Identical Filename
- Zero-Byte File
- Large File
- Database Corruption
- Disk Full
- Interrupted Operation
- Concurrent File Modification

## 23.5 Quality Gates

- Unit Tests Pass
- Integration Tests Pass
- E2E Tests Pass
- clang-format Pass
- clang-tidy Pass
- Sanitizer Tests Pass（支援平台）
- Release Benchmark Completed
- CI Pass

---

# 24. Development Roadmap

## Phase 0 — Foundation

**Goal:** 建立可靠的 C++ 工程基礎。

Deliverables：

- CMake
- vcpkg
- Project Structure
- CLI Skeleton
- SQLite Initialization
- Unit Test Framework
- GitHub Actions
- Architecture Documentation

Acceptance：

- Clean Build 成功。
- CI 能自動建置與測試。
- CLI Help 正常運作。

## Phase 1 — Core Search Engine

**Goal:** 完成原本 FileWeaver 的核心價值。

Deliverables：

- File Scanner
- SQLite Index
- Incremental Indexing
- Exact Search
- Substring Search
- Filters
- JSON Output
- Benchmark

Acceptance：

- 可索引 10 萬筆測試檔案。
- 搜尋結果正確。
- 重掃可正確更新及標記 Missing。
- Benchmark 可重現。

## Phase 2 — Daily Search Experience

**Goal:** 讓 FileWeaver 成為日常使用工具。

Deliverables：

- Qt GUI
- Global Hotkey
- Fuzzy Search
- Ranking
- Real-Time File Watching
- File Preview
- Search Result Navigation

Acceptance：

- 可透過快捷鍵開啟搜尋視窗。
- 可使用鍵盤搜尋及開啟檔案。
- 索引可隨檔案變更更新。
- GUI 不因搜尋阻塞。

## Phase 3 — File Analysis & Management

Deliverables：

- Disk Treemap
- Duplicate Detection
- Duplicate Management
- Downloads Organizer
- Batch Rename
- Dry Run
- Operation Journal
- Undo Support

Acceptance：

- 修改檔案前可預覽。
- 衝突情況能安全處理。
- 不會默默覆蓋或刪除資料。
- 可驗證 Undo 的支援範圍。

## Phase 4 — Developer Productivity

Deliverables：

- Developer Project Launcher
- Clipboard History
- Recent Activity Timeline
- Favorites
- Configurable Shortcuts

Acceptance：

- 可快速開啟 Git 專案。
- 剪貼簿歷史必須 Opt-In。
- 活動紀錄來源清楚標示。
- 使用者可清除歷史資料。

## Phase 5 — Advanced Capabilities

Deliverables：

- Local Semantic Search
- Content Indexing
- Embedding Model Integration
- Vector Search
- Snapshot Backup
- Incremental Backup
- Restore Verification

Acceptance：

- AI 搜尋不需雲端服務。
- Content Indexing 必須明確啟用。
- Backup 可通過完整性驗證。
- Restore 可在隔離測試環境完成。

---

# 25. AI Coding Agent Collaboration

## 25.1 Development Principles

採用：

**Specification-Driven Development + Modular Implementation + Automated Verification**

AI Agent 可以：

- 建立專案結構。
- 實作明確定義的模組。
- 撰寫 Unit Tests。
- 協助 Debug。
- 執行 Benchmark。
- 產生文件。
- 協助 Code Review。

開發者需要：

- 審查架構。
- 確認需求。
- 理解核心演算法。
- 驗證效能。
- 審查安全性。
- 做出技術取捨。

## 25.2 Agent Workflow

每個功能遵循：

1. Read SPEC。
2. Propose Design。
3. Identify Dependencies。
4. Define Acceptance Tests。
5. Implement Small Module。
6. Run Tests。
7. Review Results。
8. Update Documentation。
9. Commit Completed Work。

禁止一次實作全部功能而不經過驗證。

## 25.3 AGENTS.md Requirements

Repository 必須提供 `AGENTS.md`，內容至少包含：

- Project Overview
- Architecture Rules
- Coding Standards
- Build Commands
- Test Commands
- Module Ownership
- Dependency Policy
- Security Constraints
- Definition of Done

Agent 不得擅自：

- 擴大功能範圍。
- 加入雲端資料傳輸。
- 刪除使用者資料。
- 略過失敗測試。
- 偽造 Benchmark。
- 修改既有架構決策而不記錄理由。

---

# 26. Repository Structure

```text
FileWeaver/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── README.md
├── PRODUCT_SPEC.md
├── AGENTS.md
├── LICENSE
│
├── apps/
│   ├── cli/
│   ├── desktop/
│   └── background-service/
│
├── include/fileweaver/
│   ├── core/
│   ├── filesystem/
│   ├── search/
│   ├── database/
│   ├── indexing/
│   ├── analysis/
│   ├── duplicates/
│   ├── organizer/
│   ├── rename/
│   ├── clipboard/
│   ├── projects/
│   ├── activity/
│   ├── semantic/
│   ├── backup/
│   └── platform/
│
├── src/
│   ├── core/
│   ├── filesystem/
│   ├── search/
│   ├── database/
│   ├── indexing/
│   ├── analysis/
│   ├── duplicates/
│   ├── organizer/
│   ├── rename/
│   ├── clipboard/
│   ├── projects/
│   ├── activity/
│   ├── semantic/
│   ├── backup/
│   └── platform/
│
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── e2e/
│   └── fixtures/
│
├── benchmarks/
│   ├── search/
│   ├── indexing/
│   └── hashing/
│
├── docs/
│   ├── architecture/
│   ├── decisions/
│   ├── performance/
│   └── security/
│
├── scripts/
├── packaging/
└── .github/workflows/
```

---

# 27. GitHub & CV Deliverables

## 27.1 README

必須包含：

- Product Overview
- Screenshots
- Feature List
- Architecture Diagram
- Installation
- Build Instructions
- Usage Examples
- Benchmark Results
- Testing Instructions
- Privacy Statement
- Known Limitations
- Roadmap

## 27.2 Engineering Documentation

- Architecture Decision Records
- Search Algorithm Comparison
- Indexing Strategy
- Concurrency Model
- Performance Analysis
- Testing Strategy
- Security Design

## 27.3 MSCS CV Description

以下是預計完成核心功能後可採用的描述模板，必須依實際成果調整：

**FileWeaver — High-Performance File Search & Productivity System | C++20, Qt, SQLite**

- Designed and developed a local-first file indexing and search system in C++20, integrating persistent metadata storage, incremental indexing, and advanced query filtering.
- Implemented fuzzy matching, relevance ranking, and asynchronous search workflows for an interactive desktop file launcher.
- Evaluated search latency, indexing throughput, and memory usage using reproducible benchmarks and automated testing.

完成更多模組後，可補充：

- Built multithreaded duplicate detection and safe file-management workflows with operation journaling and recovery mechanisms.
- Developed cross-platform filesystem abstractions and automated build, testing, and static-analysis pipelines.

所有描述均以實際完成的功能為準。

---

# 28. Definition of Done

功能只有在符合以下條件時才算完成：

- Implementation Complete
- Acceptance Criteria Passed
- Unit Tests Passed
- Integration Tests Passed
- Error Handling Verified
- Security Constraints Reviewed
- Documentation Updated
- CI Passed
- Performance Measured（適用時）
- No Known Critical Data-Loss Bug

---

# 29. Final Product Goals

FileWeaver 的最終目標不只是快速搜尋檔案，而是建立一個：

**Fast, Local, Private, Extensible Personal File Command Center.**

產品應同時具備：

1. 高效能的 C++ 搜尋與索引核心。
2. 真正方便日常使用的桌面操作體驗。
3. 安全可靠的檔案整理與管理功能。
4. 開發者工作流程整合。
5. 可選的本機 AI 搜尋能力。
6. 完整的測試、效能分析與工程文件。

**Implementation Priority:** Core Search → Desktop Launcher → File Management → Developer Productivity → AI & Backup.

此優先順序應在整個開發期間維持，除非有經過記錄的架構或產品決策變更。