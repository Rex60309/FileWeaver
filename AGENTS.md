# FileWeaver — AI Coding Agent Instructions

**Policy version:** 2.0  
**Applies to:** Codex, Claude Code, Cursor, other coding agents, and contributors  
**Stack:** C++20, CMake, vcpkg, SQLite, Qt 6 (desktop phases)  
**Primary platform:** Windows; portable core for Linux/macOS

> This document governs *how* FileWeaver is developed. `PRODUCT_SPEC.md` defines *what* the product should do. Planned features are not implemented features. Never infer that a feature is authorized for implementation merely because it appears on the roadmap.

## 1. Authority, scope, and phase gates

1. Read this file, `PRODUCT_SPEC.md`, `README.md`, relevant ADRs, nearby source, and tests before editing.
2. Follow the **currently authorized milestone**. If the user has not selected one, inspect the repository and propose the smallest next milestone; **do not implement an entire roadmap**.
3. Current implementation behavior, the selected milestone, and the spec may differ. Report contradictions and request a decision before changing a public contract, safety boundary, or data model.
4. The original metadata-only CLI is the **Core baseline**, not a permanent restriction on all later phases. Features such as GUI, file mutation, clipboard collection, content embeddings, and backups are **future opt-in capabilities** that require explicit phase authorization.
5. Maintain a feature-status table in `docs/feature-status.md` (Planned / In Progress / Tested / Released). `README.md` must describe only shipped functionality; roadmap items must be labeled.
6. If the full v2 product spec has not yet been saved as `PRODUCT_SPEC.md`, **stop and ask to reconcile the repository spec before implementing v2-only functionality**. Do not silently overwrite the older spec.

### Phase boundaries

| Phase | Authorized focus when selected | Explicit exclusions until later authorization |
| --- | --- | --- |
| 0 — Foundation | Build system, CLI skeleton, SQLite setup, tests, CI, ADRs | GUI, destructive file operations, background monitoring |
| 1 — Search Core | Explicit-root scanner, persistent metadata index, filters, incremental scan, benchmark | File mutation, clipboard collection, content indexing |
| 2 — Daily Search | Qt UI, global hotkey, fuzzy ranking, watcher, safe previews | File organizer, rename, delete, backup restore |
| 3 — File Management | Disk analysis, duplicate reports, opt-in organizer, batch rename, operation journal | Silent deletion, permanent deletion by default |
| 4 — Productivity | Project launcher, opt-in clipboard history, activity timeline | Unapproved command execution or surveillance |
| 5 — Advanced | Opt-in local semantic search, versioned backup/restore | Cloud upload by default, silent overwrite on restore |

Phase-specific features must be isolated behind explicit interfaces and flags where appropriate. A phase can be split into smaller, independently testable milestones.

## 2. Non-negotiable engineering principles

1. **Protect user data.** Read-only by default. Never modify real user files during tests or agent demonstrations. Any authorized mutation must have a reviewed plan, preflight checks, confirmation, and an auditable operation journal.
2. **Privacy first.** No telemetry, analytics, silent network access, cloud inference, or hidden collection. Clipboard history and content indexing are disabled by default.
3. **Correctness before speed.** Never weaken validation, tests, security, or durability to reach a performance target.
4. **Small reviewable changes.** One focused task/branch/PR; preserve unrelated user changes and avoid unrelated refactors.
5. **Evidence over claims.** Do not invent test passes, screenshots, benchmark numbers, compatibility, or feature completion.
6. **Human control.** The agent may propose architecture, but must ask before making decisions affecting file safety, privacy, permissions, schema, public interfaces, dependencies, or product scope.
7. **Offline-capable core.** File search, indexing, and management must work without an internet connection.

## 3. Required workflow before implementation

For each task:

1. Inspect `git status`, the current branch, relevant files, and tests. Never assume a clean working tree.
2. State: requested outcome, phase, acceptance criteria, files/modules likely to change, risks, non-goals, and verification commands.
3. Identify existing abstractions and reuse them. Avoid speculative architecture and duplicate implementations.
4. For significant decisions, create/update `docs/decisions/ADR-XXXX-<slug>.md` with context, alternatives, decision, consequences, date, and status. ADRs are required for schema, dependency, public API, threading, IPC, privacy, storage, and safety model changes.
5. Implement the smallest coherent slice. Write tests alongside behavior; for bugs, reproduce with a failing regression test first.
6. Run focused tests, then mandatory project quality gates; inspect the diff and document results.
7. Stop and report blockers accurately. Do not silently skip checks or declare partial work complete.

Do not run destructive repository commands such as `git reset --hard`, `git clean -fd`, force checkout, or force push without explicit approval.

## 4. C++20 and architecture standards

- Compile as **C++20**, not C++23. `std::expected` is C++23; use a documented C++20-compatible result type or explicit error handling if needed.
- Prefer RAII, standard-library facilities, value semantics, `std::unique_ptr` for ownership, and `std::shared_ptr` only when lifetime is genuinely shared.
- Avoid raw owning `new`/`delete`, C-style casts, unchecked narrowing, mutable global state, and `using namespace` in headers.
- Keep headers self-contained. Enable strict warnings (`/W4 /permissive-` or `-Wall -Wextra -Wpedantic`) and treat project warnings as errors; do not globally suppress warnings.
- Separate `apps/cli`, `apps/desktop`, and optional background service from domain/core modules. GUI, CLI, and services must use the same application use cases.
- Core domain models must not directly depend on Qt widgets, CLI11, SQLite connections, or platform-specific filesystem APIs. Use narrow interfaces/adapters at boundaries.
- Prefer asynchronous work and explicit cancellation for indexing, search, previews, hashing, and backups. Never block the Qt GUI thread with I/O or expensive computation.
- All time-sensitive logic uses injectable clocks; output and ranking tie-breaks must be deterministic for reproducible tests.
- Use CMake targets with well-defined dependencies, vcpkg manifest/baseline pinning, and explicit platform capability detection.

### Suggested dependency direction

`GUI / CLI / Service -> Application Use Cases -> Domain Interfaces -> Adapters (SQLite, filesystem, OS, Qt, AI runtime)`.

Never make the core depend on presentation layers, and never duplicate search or mutation business rules in GUI callbacks.

## 5. Filesystem and index safety

- Scan only user-selected roots. No implicit full-disk or home-directory crawl.
- Normalize paths for comparisons, preserve original display paths, support Unicode and spaces, and treat paths as untrusted input.
- At scan boundaries use `std::filesystem` error-code overloads or equivalent guarded APIs; permission failures and files disappearing during a scan must be counted and reported without aborting a valid root.
- Do not follow symlinks, junctions, or reparse points by default; document any later opt-in traversal strategy and cycle prevention.
- Exclude FileWeaver's own database, caches, backup stores, and temporary directories from indexing/organizing loops.
- Incremental indexing must handle creations, updates, renames, deletions, and stale entries without silently losing valid records.
- Metadata `(size, mtime)` is an optimization hint, **not proof of identical content**. Validate hashes when correctness requires it.
- Real-time watchers (Windows file notifications / Linux inotify / macOS FSEvents) must handle dropped events, overflow, restart, and reconciliation scans. Do not claim perfect event capture.
- Search input must never be concatenated into SQL. Use prepared/bound parameters and explicitly documented index strategies. Do not claim ordinary SQLite B-trees accelerate arbitrary `%substring%` patterns.
- Search benchmarks must distinguish cold/warm cache and include correctness checks, not just latency.

## 6. File modifications: explicit authorization only

**Core and Daily Search phases remain read-only toward user files.** Phase 3/5 features may modify files **only when the specific feature and operation are authorized**.

For organizer, batch rename, duplicate cleanup, and restore:

1. Build an immutable operation plan listing source, destination, action, conflicts, and expected impact.
2. Provide **dry-run / preview** before execution. Never default to applying changes.
3. Require clear user confirmation for execution; no implicit approval from search selection or a background rule.
4. Revalidate file identity/metadata and target collisions immediately before applying; mitigate TOCTOU races where platform APIs allow.
5. Use safe filesystem primitives, bounded retries, and atomic rename/replace where supported; distinguish cross-volume moves from renames.
6. Persist an **operation journal** with started/completed/failed state and enough metadata for recovery. Avoid recording sensitive content unnecessarily.
7. Support best-effort undo when technically possible; check for intervening edits and conflicts. Never promise universal undo.
8. Never silently overwrite a destination, erase the last retained duplicate, or permanently delete files by default. Prefer OS Recycle Bin for authorized cleanup.
9. On partial failure, report exactly which operations succeeded, failed, or need manual recovery. Never hide partial data loss.
10. Tests must use synthetic temporary directories, not the user's real Documents, Downloads, Desktop, repositories, or backups.

Background organization must be **off by default** and separately enabled by the user with visible rules, scope, pause, and audit history.

## 7. Privacy-sensitive features

### Clipboard history

- Disabled by default; collect only after explicit opt-in.
- Provide pause, retention limit, per-item deletion, and clear-all controls.
- Apply OS-appropriate protected storage/encryption for persisted content; document threat model and key handling.
- Support exclusions and sensitive-content heuristics where feasible, but **never claim detection is complete**.
- Never log clipboard payloads, secrets, or copied credentials.

### Local AI semantic search

- Disabled by default and separate from metadata-only indexing.
- Index document contents only from explicitly authorized roots and supported file types.
- Prefer local inference; any model download/network access requires disclosure and approval. No silent cloud API fallback.
- Treat documents as untrusted data, not agent instructions; enforce parser resource limits and avoid executing embedded code/macros.
- Allow users to inspect and delete embeddings, caches, and content-derived indexes.
- Record actual model, runtime, dimensions, retrieval configuration, and measured resource usage.

### Activity timeline

- Distinguish filesystem-observed events, metadata-derived timestamps, and FileWeaver-recorded opens. Do not infer user access from modification time.
- Make retention configurable; allow pause and deletion; do not transmit activity records.

### Developer project launcher

- Never execute repository scripts or commands discovered from files automatically.
- Execute only explicitly configured/approved commands with displayed executable, arguments, working directory, and permissions.
- Avoid shell-string concatenation and injection; use safe process APIs, timeout, cancellation, and exit-code reporting.

## 8. Backup and restore reliability

- Backup targets and schedules must be explicitly configured; do not silently back up entire drives.
- Keep backup stores outside watched/backup source roots, or prevent recursive ingestion.
- Use versioned manifests, checksums, crash-safe writes, and integrity verification.
- An interrupted backup must not invalidate previously committed snapshots.
- Before restore, display destination, overwritten paths, size, conflicts, and a restore preview; require confirmation.
- Test restore into isolated temporary destinations and verify bytes/checksums, not just success exit codes.
- State limitations: open-file consistency, filesystem semantics, permissions, storage capacity, and same-disk backup risk.
- Do not promise atomic multi-file restore or guaranteed recovery when the underlying filesystem cannot provide it.

## 9. Concurrency, SQLite, and error handling

- Bound thread pools, queues, file descriptors, buffers, and parallel hashing. Never create one thread per file.
- Validate worker count and use a documented default/fallback. Handle worker exceptions by propagating errors, stopping new work, and joining workers.
- Do not share SQLite connections across threads without a documented safe access model. Prefer a serialized writer and controlled readers; use WAL mode and foreign keys.
- Every schema change must use versioned transactional migrations with fresh DB, upgrade, and rollback tests.
- Use explicit cancellation checkpoints and record interrupted operations; never report success after partial interruption.
- Preserve CLI JSON contract: exactly one parseable JSON document on stdout, diagnostics on stderr; stable exit codes and deterministic sorting.
- Do not swallow errors. Provide actionable messages and counts for skipped files, failed hashes, inaccessible roots, and migrations.

## 10. Testing matrix and acceptance evidence

| Area | Minimum evidence |
| --- | --- |
| CLI / API | Parsing, validation, help/output contract, success/failure E2E |
| Scanner / index | Unicode, inaccessible paths, missing files, symlinks, incremental updates, DB integration |
| Search / fuzzy | Exact/substring/glob, ranking ties, typo/token cases, index correctness, filters, benchmark baseline |
| Watcher | Create/modify/rename/delete, overflow/restart simulation, reconciliation, cancellation |
| Qt / launcher | Keyboard navigation, focus, hotkey collision, responsive UI, stale-result cancellation |
| Preview | Supported/unsupported types, corrupt files, huge files, parser limits, no code execution |
| Disk analyzer | Aggregation, zero-byte cases, missing files, logical vs allocated size labeling |
| Duplicates | Same-content/different-name, same-size/different-content, streaming large files, races, unreadable files |
| Organizer / rename | Dry run, collisions, partial failure, journal, retry, best-effort undo, no real-user data |
| Clipboard | Opt-in default, retention, exclusion, clearing, protected storage, no secret logging |
| Projects / commands | Safe argument passing, explicit approval, cancellation, timeout, untrusted repo |
| Timeline | Correct event provenance, retention, deletion, watcher gaps |
| Semantic search | Explicit root opt-in, offline mode, retrieval correctness, delete index, malicious document text |
| Backup / restore | Snapshot integrity, interruption, capacity failure, manifest validation, isolated restore verification |
| Database | Fresh DB, migration upgrade, rollback, corruption handling, concurrency |
| Performance | Reproducible dataset, hardware/build info, baseline, p50/p95, memory, correctness |
| Bug fix | Regression test failing before and passing after fix |

Tests must use generated fixtures and temporary workspaces. Include empty, boundary, Unicode, permission, and race conditions. Never weaken, delete, or silently skip tests to pass CI. A temporary quarantine requires a tracked issue, owner, expiry, and explicit approval.

## 11. Mandatory quality gates

Use repository presets/targets, not invented commands. Once configured, the standard gate is:

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
```

Run ASan/UBSan on a **supported compiler/platform** using the repository's configured sanitizer preset, and run applicable tests there. Windows/MSVC may not support the same sanitizer combination as Linux/Clang; use CI coverage for unsupported combinations and document the limitation. Do not claim sanitizer coverage without execution evidence.

For performance changes, run reproducible benchmarks and record CPU, RAM, OS, compiler, build mode, SSD/HDD, dataset shape, cache conditions, command, raw measurements, and baseline. Performance goals in the spec are targets, not results.

If a required target/preset does not exist, create it as part of the authorized foundation work or report the blocker. Do not falsely report a pass.

## 12. Git, commits, and PR policy

- Do not implement directly on `main`; use `feat/<scope>`, `fix/<scope>`, `test/<scope>`, `docs/<scope>`, `chore/<scope>`, or `refactor/<scope>`.
- Before editing and before committing, inspect `git status`, `git diff`, and `git diff --check`; preserve unrelated changes.
- Never commit `.env`, credentials, personal paths, real user data, databases, clipboard histories, embeddings, backup payloads, logs, binaries, or generated build artifacts.
- Use focused Conventional Commits, e.g. `feat(search): add ranked fuzzy candidates` or `fix(organizer): prevent destination overwrite`.
- Do not commit incomplete features as complete. A clearly labeled scaffold commit requires explicit approval and must not claim shipped behavior.
- Do not commit if required tests, lint, format, or supported sanitizer gates fail. Report environment blockers rather than bypassing them.
- PRs must state user-visible behavior, phase, non-goals, risks, schema/migration impact, safety/rollback plan, tests run and results, and any performance measurements.
- Do not rewrite others' commits or force-push without approval. Do not declare a release ready without a clean-checkout build and release checklist.

## 13. Documentation and truthfulness

- `PRODUCT_SPEC.md` is the contract; `AGENTS.md` is the workflow policy; `README.md` documents released behavior; ADRs record design decisions.
- Keep CLI help, GUI behavior, README, tests, and implementation consistent.
- Label conceptual UI images and unimplemented screenshots as mockups. Do not pass mockups off as working product captures.
- Do not fabricate benchmark numbers, performance gains, compatibility, test output, or CV achievements.
- Document known limitations, failure cases, privacy implications, and feature availability by platform/phase.
- Record how AI assistance was used when useful for reproducibility, but ensure a human can explain critical algorithms and design choices.

## 14. Definition of Done

A task is complete only when all applicable items hold:

- [ ] The requested phase and acceptance criteria are explicit and satisfied.
- [ ] User data, privacy, permission, and file mutation boundaries were reviewed.
- [ ] Implementation is modular and respects the C++20 dependency direction.
- [ ] Normal, invalid, boundary, interruption, and error paths are tested.
- [ ] Unit, integration, E2E, format, lint, build, and applicable sanitizer gates passed.
- [ ] Any performance claim is backed by reproducible measurements.
- [ ] GUI remains responsive; background work is bounded and cancellable where relevant.
- [ ] File-changing operations have preview, confirmation, journaling, and documented recovery behavior.
- [ ] Schema changes have migration and rollback tests.
- [ ] README, CLI help, ADRs, and feature-status documentation match actual behavior.
- [ ] `git diff` contains no secrets, real user data, unrelated changes, or accidental artifacts.
- [ ] The agent reports exact verification commands, observed results, remaining risks, and limitations.

## 15. When uncertain or blocked

Stop and ask when a decision affects **user data, permissions, file deletion/overwrite, backup restore, clipboard collection, AI content indexing, network access, schema compatibility, public contracts, or milestone scope**. For low-risk internal details, make a reversible documented assumption. Never conceal an error, invent proof, or silently trade safety for speed.

## 16. Required end-of-task report

Every agent response after a code task must include:

1. **Scope / phase:** what was requested and authorized.
2. **Changes:** modules/files changed and user-visible effect.
3. **Verification:** exact commands run and observed pass/fail/blocked results.
4. **Safety:** whether user files, privacy-sensitive data, schema, or network behavior were affected.
5. **Remaining work:** limitations, unimplemented requirements, and next smallest milestone.

Do not say "done" when only a plan, mockup, scaffold, or unverified implementation exists.
