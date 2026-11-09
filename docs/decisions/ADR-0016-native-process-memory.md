# ADR-0016: Native benchmark process memory high-water marks

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1 measurements

## Context and alternatives

ADR-0015 reports native timings but leaves core_peak_rss_bytes null. Polling child
processes from Python can miss short-lived peaks. Prefer operating-system lifetime
high-water counters sampled inside the helper; do not estimate allocation or
subtract two high-water marks to claim per-query memory consumption.

## Decision

Keep the adapter private to benchmarks/search. Windows reads the current process's
GetProcessMemoryInfo/PeakWorkingSetSize (bytes). Linux reads
getrusage(RUSAGE_SELF).ru_maxrss (KiB), converting to bytes with overflow checks.
Other platforms return null with unsupported_platform. This is capability-limited
metadata, not a platform support claim for the whole product. No new third-party
dependency, domain API, database or fileweaver CLI contract is introduced.

Sample once before oracle/sample construction and once after all search/result
checks, before report serialization. Both calls are outside timing intervals.
Report before/after lifetime peaks, metric/source/unit/scope and individual status.
Failures or nonpositive/overflowing counters remain null with explicit reasons.
Never report unavailable data as zero, and never interpret the difference as
allocated bytes, a leak, or a pure search-only peak. The process includes runtime,
argument parsing, fixture oracle, result validation and sample buffers. Reporting
serialization/output and teardown after the second snapshot are excluded.

The Python report keeps version 2, adds core_memory metadata and fills its existing
core_peak_rss_bytes from the after-workload snapshot when available. Older helpers
without memory metadata remain accepted and report null with an explicit missing
metadata reason. Reports without --core-runner retain null native values. Historical
reports and their hashes/measurements are not rewritten.

## Consequences and evidence boundaries

No contents, user roots, other-process statistics or network data are collected.
Only the benchmark's own process is queried; fixtures remain generated temporaries.
Tests check positive monotonic counters on supported systems and explicit nulls on
unsupported ones, and retain all search correctness/argument/report safeguards.
Actual OS-query failure cannot be reliably forced against one's own process in
this E2E environment; the guarded status branch is not claimed as executed.
Windows evidence is collected locally; Linux execution awaits CI. Native working
set/RSS counters have platform-specific semantics and exclude unresident allocations
and system-wide SQLite/filesystem caches. They are not universal memory accounting.
CLI peak RSS, controlled cold-cache runs and optimized-index comparisons remain.

Follow-up ADR-0017 adds opt-in observations of actual CLI children, separately from
native helper peaks and timed samples. Historical memory reports remain unchanged.

## API references

- [Microsoft PROCESS_MEMORY_COUNTERS](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters)
- [Linux getrusage manual](https://man7.org/linux/man-pages/man2/getrusage.2.html)
