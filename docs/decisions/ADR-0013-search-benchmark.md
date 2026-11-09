# ADR-0013: Synthetic search timing baseline

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1

## Context and alternatives

There is no measured search baseline. A core-only C++/Google Benchmark harness is
still needed for spec latency targets. As a first independently verifiable slice,
use a stdlib Python developer runner against the real Release CLI and explicit
Python baselines. Do not label process/Python timings as interchangeable core times.

## Decision

Generate 1..100000 unique zero-byte ASCII .txt files in an owned temporary flat
directory. Use real init/index and verify the indexed count. Run exact first/last/
absent and substring prefix/category/absent queries, each limited to 100. An
independent deterministic filename oracle verifies exact ordered name/path/size
tuples on every timed call. CLI uses actual JSON output; baseline SQLite uses
NOT INDEXED and bound values; filesystem baseline uses scandir plus top-100 filename
selection and selected-file stats, not the C++ scanner. Close SQLite per query.

Measure perf_counter_ns around complete method calls, excluding result comparison.
CLI includes process startup, search, JSON, Python decoding/materialization. Baselines
are Python implementations with different envelopes/SQLite builds. Rotate method
order. Record first-observed, warmup and measured samples separately; do not flush
OS caches or call any result cold-cache. Record nearest-rank p50/p95 pooled across
the three queries per mode, plus raw samples/query distribution.

CMake generates build-info per configuration beside the executable. Require Release
unless --smoke is explicit. Record compiler/version/build/platform/C++ standard,
binary SHA-256, CPU/logical count/RAM, OS, Python/SQLite versions, operator storage
label, driver peak RSS where available, and explicitly unmeasured CLI peak RSS.
Unknown hardware/storage values remain unknown. No telemetry/network dependencies.

Write a new JSON report exclusively; never overwrite an existing report. Keep
synthetic files/databases in a unique temporary directory and clean only that
owned fixture. No real user roots or data. Do not commit personal paths, generated
databases or executables. Reports omit temporary paths and computer/user names.
Raw reports can remain under ignored build; sanitized measured evidence may be
recorded in docs. Timing samples are not test assertions or CI performance gates.

## Consequences

This is a developer script, not a shipped `fileweaver benchmark` subcommand.
No production dependency/schema/API change. A tiny CTest smoke verifies correctness,
report shape, argument bounds, Debug guard and no-overwrite behavior. Report timings
are evidence only for their stated envelopes; no spec p95 target compliance,
optimized trigram baseline, controlled cold-cache, CLI RSS or cross-machine speedup
is claimed. Core-only benchmarking, larger/varied datasets and remaining quality
gates must precede a Phase 1 completion claim.
