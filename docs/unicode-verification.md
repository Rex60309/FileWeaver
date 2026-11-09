# Unicode filename verification — 2026-11-08

Scope: Phase 1 bug fix, authorized multilingual filenames and CLI paths/queries.
No schema, search matching semantics, source-file mutation or network changes.
Implementation: apps/cli/windows_io.hpp, CLI entry point, CMake registration/linking,
tests/e2e/unicode_contract.py; README, feature status and ADR-0014 updated.

## Regression and coverage

Before the fix, `ctest --preset debug --output-on-failure -R unicode-contract`
failed: human `search` with an emoji-only query returned 2 and
`Invalid arguments: --database: 1 required TEXT missing`.
The CLI had mixed Windows Unicode argv with the narrow CRT argc.

The final generated fixture checks Chinese/Japanese/spaces/emoji in source and
database paths, a mixed Chinese/Japanese/accent/emoji filename, individual literal
queries, full-name exact matching, JSON name round trips and human UTF-8 pipes.
A separate BMP filename is checked in an isolated hidden CP950 console using
ReadConsoleOutputCharacterW; console code page must remain unchanged.
Legacy screen buffers may replace supplementary characters, so emoji fidelity is
checked through UTF-8 pipes rather than asserting a specific screen glyph.
Only owned synthetic temporary files/databases are written. No personal roots are
scanned and no system/parent console settings are changed.

## Commands and observed results

Windows GCC 13.2, C++20; configured repository dependency/tool caches.

```powershell
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --test-dir build/release --output-on-failure
git diff --check -- . ':!AGENTS.md' ':!PRODUCT_SPEC.md'
```

Debug/Release build and both suites passed: 5/5 CTest groups each. Format and lint
passed. Initial lint caught an implicit multilevel pointer conversion in the
LocalFree deleter; corrected to an explicit void-pointer cast before the passing
rerun. No warning suppression was added.
There is no release test preset: an attempted `ctest --preset release` failed;
the actual Release suite was subsequently run with `--test-dir` as shown above.

`cmake --preset sanitizers` was attempted and blocked by the default old MinGW
compiler's system DLL relocation failure. The repository combined ASan/UBSan
configuration also excludes Windows; supported Linux/Clang CI execution remains
pending. No sanitizer pass or cross-platform runtime verification is claimed.
No commit/release acceptance is claimed while that evidence remains pending.

Diff whitespace check excludes unchanged supplied AGENTS.md/PRODUCT_SPEC.md with
existing trailing spaces. No generated artifacts or real data are staged/committed.
Historical search timings still describe their recorded binary hash, not this
modified binary. No performance improvement is claimed for this change.

Remaining: terminal/font-dependent glyph rendering; Unicode normalization and full
case folding are not implemented. Next smallest related milestone is a supported
Linux sanitizer/CI verification, without expanding the product phase.

## Follow-up: nested names and conversion diagnostics

User reported `index` on a tree containing Japanese directories and punctuation.
Generated nested directories with kana, kanji, fullwidth parentheses and
`〖〗⌇⁉…♡×` filenames, plus a relative database path, indexed successfully.
No real source tree was accessed, and no private filenames/payloads were added to
the repository. A synthetic unpaired UTF-16 surrogate filename reproduces the
exact `Cannot convert character sequence: Illegal byte sequence` exception.
That is a reproducible failure class, not a confirmed diagnosis of the user's tree.

Before the follow-up fix, the expanded `unicode-contract` failed its assertion
requiring an actionable path. Afterward it passes, requiring a strict UTF-8
diagnostic with `invalid-\uD800.txt`, failure exit code 1, one generic JSON error,
the previous three indexed files preserved and a newly observed valid file absent
after rollback. The malformed fixture's bytes remain unchanged.
Storage path conversion diagnostics now escape non-ASCII/control native units.
No exception swallowing, source mutation, schema migration or silent skipping.
README documents how to provide the escaped path for further diagnosis.

Follow-up verification commands were the same Debug configure/build/tests,
format-check, lint, Release configure/build, Release --test-dir tests and scoped
diff check listed above. Both final suites passed 5/5 groups; Debug/Release builds,
format and final lint passed. The first follow-up lint flagged integer promotion
in native-unit hex escaping; corrected with uint32_t and unsigned shift arithmetic,
without suppression. Sanitizer/CI limitations remain as documented above; they
were not rerun or claimed passed for this follow-up. No commits were made.

The real reported tree's offending entry remains unconfirmed. Next smallest step:
rerun the updated Release executable against the user's chosen root and inspect
the new escaped diagnostic before deciding on any filename/storage handling change.
