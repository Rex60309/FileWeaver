# Feature status

No features are released. In Progress does not imply build or test acceptance.

| Phase | Feature | Status |
| --- | --- | --- |
| 0 | Git, C++20 build presets, CLI help and contract tests | Tested |
| 0 | Format and lint gates (Windows GCC 13.2) | Tested |
| 0 | Pinned vcpkg, SQLite initialization, Catch2 integration tests | Tested |
| 0 | Architecture and schema ADRs | Tested |
| 0 | Hosted Windows/Linux CI (execution pending) | In Progress |
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
| 2 | Qt, hotkey, fuzzy search, watchers, safe preview | Planned |
| 3 | Analysis, duplicates, organizer, rename, journal | Planned |
| 4 | Project launcher, opt-in clipboard, activity | Planned |
| 5 | Opt-in semantic search, backup and restore | Planned |
