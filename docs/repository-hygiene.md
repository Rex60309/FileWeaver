# Repository hygiene and bilingual README audit

Date: 2026-11-12. Scope: documentation and ignore rules only; no application,
schema, dependency or user-file behavior changes. User requested concise current
features in Chinese/English, an ignore audit and a local commit.

## Changes

README.md is the concise Traditional Chinese entry point; README.en.md provides
the matching English version. Both document current functionality, UI/CLI quick
starts, build prerequisites and important limitations without phase headings.
CLI examples explicitly initialize their catalog before indexing. Detailed
verification and historical measurements remain linked in docs.

.gitignore already excluded build/, vcpkg installation, SQLite/.db sidecars,
Python caches and local .env files. Additional rules cover root out/, cmake-build-*,
dist/, tmp/, output/, accidental in-source Ninja/CMake output, compiled libraries,
executables/debug symbols, .sqlite3 sidecars, logs and editor/OS temporary files.
No broad JSON/image/test-directory exclusion was added. Branding PNG/ICO/SVG,
source/tests, curated benchmark JSON/Markdown, build configuration and .env
example/template files remain eligible for Git.

## Executed checks and observed results

- git status --short --branch: clean before edits; changes isolated to this task.
- git ls-files -ci --exclude-standard: empty before and after rule changes.
- git check-ignore --no-index -q -- <path>: 46 generated/local path cases ignored;
  17 source/test/asset/report/template path cases retained. No fixture files were
  created; checks used path strings only.
- Read-only tracked-file audit: no executables, databases or logs were tracked;
  the only binary/image candidates were the intentional logo PNG and ICO.
  No tracked file exceeded 5 MiB. This is not a full historical secret audit.
- Python pathlib/re validation: both README files decode as UTF-8, contain no
  unintended control bytes, have balanced code fences, resolve all 16 relative
  link occurrences, have no phase headings and include the CLI init command.
- git diff --check: passed after final edits.

The initial documentation check matched the word phase1 in a historical benchmark
link target; the check was corrected to inspect headings, preserving the verified
benchmark path. Native sources are unchanged from d2f49f2, whose core Debug/Release
13/13 and desktop 16/16, format and lint evidence is in search-layout-verification.md.
No C++ rebuild/test, benchmark or hosted Actions run was performed for this
non-runtime edit. All owned-repository Actions remain paused; no push is requested.

## Limits

Ignore rules prevent ordinary accidental additions, but do not remove already
tracked files, sanitize history or detect all secrets. New tools/intentional binary
fixtures may need targeted rule changes. Performance documents describe their
original source, timing envelope and cache conditions; they are not fresh results
for the latest UI. Before CV performance claims, rerun the current Release with
synthetic datasets and record p50/p95, indexing time, process memory, build/hardware,
cache conditions and independent result correctness. Do not infer a speedup over
Windows Explorer or an indexed competitor from the existing Python baselines.
