# Windows folder-first indexing verification

Date: 2026-11-12 (Asia/Taipei).
Status: implementation and local verification in progress; no final acceptance claim.

## Authorized scope

The user explicitly approved Add search folder, managed Windows application-local
storage, a cancellable background indexing job, automatic bounded file listing and
continued access to existing indexes. ADR-0039 records the storage/threading/UI
decision. Linux development, new dependencies, schema changes, content indexing,
file management and release packages are excluded.

The managed catalog is `catalog/index.sqlite` below Qt AppLocalDataLocation.
Selecting an external SQLite file remains read-only search; adding a folder uses
the managed catalog and never silently rewrites that external selection. Startup
selects the managed path but does not create it or rescan sources. A missing
managed catalog is an empty first-run view; corrupt/inaccessible catalogs remain
errors. All tests inject temporary catalog paths rather than writing real AppData.

## Implementation and safety

- IndexController has one persistent worker, one active/pending job and one
  completion. Duplicate submissions are rejected until GUI delivery. UTC clocks
  and the indexing adapter are injectable. Qt only receives bounded status/counts.
- Reuse initialize_database/index_directory; all storage and traversal stay off
  the GUI thread. Pause searches during creation to avoid transient initialization
  reads; prevent index selection and watcher starts until completion.
- Require absolute, available local source/storage paths. Reject reparse/offline
  ancestors, a root containing managed storage, linked storage and orphan sidecars.
  Reserve new files with CREATE_NEW. Existing nonempty catalogs must be v3; no
  implicit schema migration. A data-free zero-byte failed-initialization file can
  be retried only after validation and with no sidecars.
- Re-adding a registered root retains its saved depth/exclusions and expected root
  identity. Cancellation/incomplete traversal retains the last effective file
  index; an empty newly initialized catalog and scan-status history may remain.
  Never delete storage automatically on cancellation or report an incomplete scan
  as success. Explicitly enabled monitoring/preview/hotkey behavior is unchanged.
- Empty GUI query builds a bounded glob `*` request with existing filters, default
  100/configurable up to 1000, name sorting unless another valid sort is selected.
  Relevance falls back to name for browsing. CLI/core empty-query validation stays.
- The original explicit standard window icon overrode the approved app logo;
  the window now inherits the QApplication icon.

## Generated regression evidence

Focused onboarding suite passed 11 cases including init/cleanup, zero failures,
skips or blacklisting. It covers Unicode/emoji, case variants, saved exclusions,
new/zero-byte/corrupt/read-only catalogs, preservation of an external catalog,
stray sidecars, directory junctions and hard links, missing/invalid roots, invalid
clock, pre-cancellation, cancellation during the actual use case, incomplete-scan
rollback, bounded worker/cancellation/errors, GUI responsiveness, Cancel-default
confirmation, 140 indexed files limited to 100, clearing a query, reopening without
rescanning, competing-action blocking, and window icon inheritance.

Actual failing regressions before fixes:

- Legacy window standard icon failed the QApplication icon cache-key comparison.
- The initial taller controls squeezed the result viewport below 150 px; merging
  the source bar, reducing spacing and selecting a screen-bounded default size
  restored a useful result list. The viewport regression then passed.
- The old doesNotSearchImplicitly test expected zero searches after selecting a
  catalog. It was replaced with an explicit new-contract test: no calls without
  catalog selection, then glob `*`, limit 100 and name sorting after selection.
  No tests were removed or skipped.

An initial test compile rejected ignored nodiscard results and a misnamed junction
fixture helper; explicit void helpers and the existing directoryLink API fixed
those errors without disabling warnings. Qt minimal emits its known size-hint
warning; all actual test cases still pass. For private visual inspection only,
FILEWEAVER_ONBOARDING_CAPTURE enables minimal:enable_fonts and Fusion palette;
the default minimal dummy font initially produced unreadable rectangles. The
resulting capture uses generated data and the actual widget, not a product mockup
or a native Windows theme screenshot. It remains ignored under build.

## Exact local commands and environment

Windows 11, GCC 13.2.0, Qt 6.11.3, CMake 3.29.2. Standard build directories and
installed dependencies are reused; no new large build/dependency copies.
Windows again locked the old phase2_tests.exe. The ignored local include below
changes only its output filename, not test content, target or CTest coverage:

```cmake
function(fileweaver_local_test_output)
  if(TARGET phase2_tests)
    set_target_properties(phase2_tests PROPERTIES OUTPUT_NAME phase2_tests_onboarding_final)
  endif()
endfunction()
cmake_language(DEFER CALL fileweaver_local_test_output)
```

```powershell
$env:TEMP=Join-Path $PWD 'build/test-temp'
$env:TMP=$env:TEMP
cmake --preset desktop-debug "-DCMAKE_PROJECT_FileWeaver_INCLUDE=$PWD/build/local-test-output.cmake"
cmake --build --preset desktop-debug --parallel 4
ctest --preset desktop-debug --output-on-failure
cmake --build --preset desktop-debug --target format-check
cmake --build --preset desktop-debug --target lint
cmake --preset desktop-release "-DCMAKE_PROJECT_FileWeaver_INCLUDE=$PWD/build/local-test-output.cmake"
cmake --build --preset desktop-release --parallel 4
ctest --preset desktop-release --output-on-failure
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
cmake --build --preset debug --target format-check
cmake --build --preset debug --target lint
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
```

After Windows releases old images, the local hook can be removed with
`cmake --preset desktop-debug -U CMAKE_PROJECT_FileWeaver_INCLUDE` (and the
desktop-release equivalent). Hosted CI uses normal target filenames.

Current observed results: core Debug/Release 13/13 each and core format/lint passed;
desktop Debug/Release 16/16 each, including onboarding, passed; desktop format
passed. Final source review, diff whitespace checks, desktop format and desktop lint passed. Supported portable-core
sanitizer/hosted coverage will be recorded separately; Windows GUI combined
ASan/UBSan is unsupported here. No performance gain or formal release is claimed.

## Final regression observations

The final desktop Debug run initially failed the existing preference font-save
test (observed 10 instead of 15). No assertion, timeout or warning policy was
weakened. Its focused rerun and five additional consecutive focused runs passed;
a full Debug rerun passed 16/16 and Release passed 16/16. The intermittent cause
remains unconfirmed and is a known verification limitation, not a proven fix.
Final logs: build/onboarding-debug-tests-recheck.log and
build/onboarding-release-tests-verified.log. The first lint run rejected an
unchecked optional and deprecated Qt exception macros; explicit optional guarding,
QVERIFY_THROWS_EXCEPTION and explicit void helpers replace them without suppression.

## Local acceptance checkpoint

Final desktop lint exited 0 (build/onboarding-lint-verified.log); format-check
exited 0 (build/onboarding-format-verified.log). Core Debug/Release 13/13 each;
final desktop Debug/Release 16/16 each. No tests were skipped. This is local
Windows GCC acceptance only. Current onboarding source has not yet run hosted
MSVC or portable-core sanitizer CI; earlier source CI is not evidence for this
change. Combined ASan/UBSan desktop coverage is unsupported locally. No release
package was requested or produced.
