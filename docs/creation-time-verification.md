# Windows creation-time verification — 2026-11-09

Selected Phase 1 gap: SPEC 3.2 creation time when supported. Explicit user approval
recorded before changing the prior unconditional-NULL writer behavior. See
[ADR-0025](decisions/ADR-0025-windows-creation-time.md).

## Implementation

The storage adapter reads native creation metadata through a metadata-only RAII
handle. Final-component reparse points/directories are rejected. Unix nanoseconds
are overflow-checked before narrowing. Unknown values are NULL for new files;
later unavailable reads retain previously known values. Explicit rescans backfill
available timestamps into old NULL rows and count one metadata update. No source
contents, writes, new API fields, CLI output changes or schema migration.

## Regression first

```powershell
cmake --build --preset debug --parallel
.\build\debug\storage_tests.exe "[creation-time]"
```

Before implementation: the native Unicode-file assertion failed, sqlite3_exec
returned constraint error 19 because indexed created_at was NULL. After
implementation: 2 Windows cases / 31 assertions passed.

The full suite then caught an older test requiring created_at to always be NULL.
That assertion now requires a known timestamp on Windows and NULL on portable
adapters; it was updated to the user-approved contract, not removed. Initial lint
also rejected signed integer constants in bitwise operations. Explicit unsigned
operands fixed those diagnostics without suppression. Subsequent full Windows
Debug/Release each passed 11 groups, storage 63 cases / 801 assertions; format and
lint passed.

The first case independently observes GetFileAttributesExW and checks exact
persisted nanoseconds, NULL backfill, unchanged subsequent scan and schema v3.
The second uses only generated temporary files and explicitly sets their native
creation attributes to test Unix epoch 0, -100ns, the largest representable
100ns-aligned positive value, upper/lower overflow becoming NULL, and retaining
a known timestamp when a later observation is unrepresentable. The product never
sets file timestamps. On Linux, the portable branch asserts NULL and unchanged
rescans; Windows-only boundary tests are not claimed to execute there.

Full Windows/Linux Debug/Release, format, lint, ASan/UBSan, hosted CI and current
100k benchmark results are consolidated in [Phase 1 acceptance](phase1-acceptance.md).

## Limitations

This is filesystem creation metadata, not proof of first content creation or user
activity. Other software can alter it. Only Windows has a native adapter in this
slice; Linux/macOS retain NULL. Unavailable native reads are optional unknown
metadata, not a fabricated timestamp. Path-based ancestor replacement races remain
outside the containment guarantee. Per-file handle reads add indexing work; no
unchanged-throughput or latency improvement is claimed.
