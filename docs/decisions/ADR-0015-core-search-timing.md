# ADR-0015: In-process application search timing

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1

## Context and alternatives

ADR-0013's CLI process/Python baseline cannot isolate C++ application search time.
Reuse the generated fixture and existing search_files use case rather than create
another search implementation or add Google Benchmark solely for this slice.
Controlled cold caches and an optimized-index comparison remain separate work.

## Decision

Build a developer-only search_core_benchmark executable alongside the CLI. The
existing stdlib Python driver optionally invokes it using --core-runner, after
the process/Python baselines on the same owned synthetic database. Require the
helper and CLI/build-info to be in the same build directory. Retain the original
three-method run when the option is absent. This adds no fileweaver subcommand,
domain API, schema, source-file mutation or external dependency.

Use steady_clock around each complete search_files call, including connection
open, read transaction, schema checks, statement preparation and result vector
construction. Exclude process launch, request/oracle construction, result vector
destruction, correctness checks and JSON serialization. This is application
search timing, not raw SQL-only timing or a reused-connection benchmark.

Use the same six exact/substring cases and 100-result cap as the driver. Construct
a deterministic native oracle from generated filenames, checking ordered complete
name/path/zero-size tuples on every call outside timing. The Python driver also
checks returned counts against its independent oracle. Fail before emitting a
success document if any result differs. Tiny tests include Unicode fixture roots,
one-file duplicate query cases, invalid arguments and a deliberately wrong size.

Record first observation in the native process, warmups and measured samples.
The fixture and preceding baselines warm OS caches; never label these cold.
Add native binary hash, actual linked SQLite version, timing envelope and explicit
unmeasured peak RSS to the report. Reports with native samples use version 2;
original three-method reports remain version 1. Percentiles remain nearest rank,
pooled across three queries per mode; raw per-query measurements are retained.
No speed ratio or SPEC latency compliance is inferred across different envelopes.

## Consequences

The benchmark helper only opens the passed database via the read-only search use
case; Python creates/indexes/cleans its synthetic fixture and exclusively writes a
new report. No real roots are used by the driver. Result reports omit paths.
Project strict warning, format, lint and supported sanitizer settings include the
helper. Core-only warm timing coverage improves, but controlled cold cache,
native/CLI peak RSS, optimized index baseline, larger varied datasets and supported
Linux sanitizer execution remain outstanding.

Follow-up ADR-0016 adds native-process memory high-water marks. Historical reports
from this timing slice retain their original unmeasured-memory fields.
