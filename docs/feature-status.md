# Feature status

No features are released. In Progress does not imply build or test acceptance.

Phase 1 (PRODUCT_SPEC section 24) implementation and acceptance completed on
2026-11-09. [Acceptance ledger](phase1-acceptance.md) records the verified source
revision, executed CI, benchmark scope and remaining whole-product limitations.
The selected Phase 2 Windows search window is tested locally and in hosted MSVC
Debug/Release CI; [evidence](desktop-search-verification.md) records the verified
revision. The authorized Windows Phase 2 first-batch scope passed local and hosted checks;
[acceptance evidence](phase2-acceptance.md) identifies the verified revision.
Multi-root/CLI watch and later phases remain planned. The separately authorized
PDF first-page extension is under verification; see [evidence](pdf-preview-verification.md).
The separately authorized GIF first-frame extension passed local and hosted
Windows checks; see [verification evidence](gif-preview-verification.md).
This is not a formal release.
The Windows global-hotkey slice also passed native interaction and hosted MSVC
Debug/Release checks; see [its acceptance evidence](global-hotkey-verification.md).
Confirmed supported-file opening passed Windows GCC/native observation and hosted
MSVC Debug/Release on 2026-11-10; see [evidence](file-opening-verification.md).
Further work is prioritized for Windows; release packaging is out of scope.

| Phase | Feature | Status |
| --- | --- | --- |
| 0 | Git, C++20 build presets, CLI help and contract tests | Tested |
| 0 | Format and lint gates (Windows GCC 13.2) | Tested |
| 0 | Pinned vcpkg, SQLite initialization, Catch2 integration tests | Tested |
| 0 | Architecture and schema ADRs | Tested |
| 0 | Hosted Windows/MSVC and Ubuntu/GCC Debug/Release, Clang format/lint/ASan/UBSan | Tested |
| 0 | Ubuntu WSL2 GCC Debug/Release and combined ASan/UBSan execution | Tested |
| 1 | Streaming metadata scanner library, exclusions, cancellation and junction protection | Tested |
| 1 | Schema v2 roots/files/scans, v1 upgrade, rollback and constraints | Tested |
| 1 | CLI index, persistent metadata, conservative incremental re-scan and failure/cancel rollback | Tested |
| 1 | Exact/literal substring filename search, present-only results, deterministic limit | Tested |
| 1 | Composable extension/inclusive-size/exact-root filters | Tested |
| 1 | Strict UTC modification-time filters and overflow-checked time parser | Tested |
| 1 | Name/size/modified sorting, descending primary and deterministic ties | Tested |
| 1 | Single-document JSON success/error/help output with strict UTF-8 and int64 tests | Tested |
| 1 | Windows Unicode argument count, multilingual filenames/queries, UTF-8 pipes and native console output | Tested |
| 1 | Nested multilingual punctuation fixtures and escaped conversion-failure paths with index rollback | Tested |
| 1 | Schema v3 saved policies, safe legacy upgrade and per-root refresh | Tested |
| 1 | Read-only roots/policy and index-count inspection with v2/v3 compatibility | Tested |
| 1 | Synthetic CLI/Python search baseline, 100k-file correctness and raw timing report | Tested |
| 1 | Native application-search timing, linked SQLite version, per-call correctness and 100k-file baseline | Tested |
| 1 | Native benchmark memory high-water marks, explicit source/scope/status and Windows 100k-file evidence | Tested |
| 1 | Opt-in Windows CLI child memory, per-case correctness/status and 100k-file evidence | Tested |
| 1 | Nested Unicode benchmark profile, duplicate-name/cutoff correctness and 100k-file evidence | Tested |
| 1 | Mixed-size/suffix/time matrix, offline snapshot correctness and explicit-empty CLI filter fix | Tested |
| 1 | Incremental filter lifecycle, reconciliation counters and stable/resurrected IDs | Tested |
| 1 | Failed/incomplete index retention, per-root partial refresh and synthetic recovery | Tested |
| 1 | Cooperative scanner/index/refresh checkpoints, snapshot retention and recovery | Tested |
| 1 | Literal filename prefix mode, ASCII case-insensitive, composable filters | Tested |
| 1 | Prefix Missing/reappearance, v2 read compatibility and pre-cancellation/retry regressions | Tested |
| 1 | Available Windows creation time, NULL backfill and overflow-safe conversion | Tested |
| Future | Controlled cold-cache/specialized-index benchmarks and cross-platform CLI memory | Planned |
| Future | Additional search modes/filters and root management commands beyond Phase 1 deliverables | Planned |
| 2 | Opt-in Windows Qt index chooser/search window, bounded async worker, keyboard selection and explicit path copy | Tested |
| 2 | Opt-in configurable Windows global hotkey, collision handling and search focus | Tested |
| 2 | Confirmed opening of selected supported Windows local files | Tested |
| 2 | Confirmed Windows Explorer reveal of selected local files, all extensions | Tested |
| 2 | Bounded fuzzy filename matching/ranking, configurable CLI and Windows UI | Tested |
| 2 | Opt-in single-root Windows notifications and policy-preserving reconciliation | Tested |
| 2 | Glob/literal path/fuzzy parent components and visual filters/sort | Tested |
| 2 | Opt-in isolated text/source/PNG/JPEG previews, bounded thumbnails | Tested |
| 2 | Escape hide with active hotkey and optional non-sensitive preferences | Tested |
| 2 extension | Opt-in GIF first-frame preview, explicit static label and existing limits | Tested |
| 2 extension | Opt-in isolated PDF first-page preview using pinned Poppler | In Progress |
| Future | Multi-root/CLI watch and PDF navigation/interaction | Planned |
| 3 | Analysis, duplicates, organizer, rename, journal | Planned |
| 4 | Project launcher, opt-in clipboard, activity | Planned |
| 5 | Opt-in semantic search, backup and restore | Planned |
