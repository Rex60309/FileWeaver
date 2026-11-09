# ADR-0017: Opt-in CLI child-process memory observations

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1 measurements

## Context and alternatives

Native helper memory does not measure the actual CLI executable. Periodic RSS
polling can miss short processes/peaks, while adding instrumentation to the product
CLI changes the observed executable. A local Windows probe confirmed that a retained
query handle can return PeakWorkingSetSize after process termination. Do not claim
this retrieval succeeds on every system; represent failures explicitly.

## Decision

Add developer-only --cli-memory to the Python fixture runner. After timed baseline
and optional native calls, launch six additional real CLI searches, one per case,
and verify full ordered name/path/size tuples. Observe only these direct children,
never enumerate or attach to unrelated processes. Use argument lists with no shell.

On Windows, open a separate handle using PROCESS_QUERY_LIMITED_INFORMATION to the
Popen-created PID immediately after launch. Retain it through communicate/exit,
query GetProcessMemoryInfo.PeakWorkingSetSize, and close it on every path. Do not
access private Popen handles. Timeout/exception paths kill and reap the child before
propagating the error. On unsupported platforms, run/validate the same commands but
report unsupported_platform and null memory. Windows API/open/query/zero-counter
failures similarly keep explicit status/error codes, never substitute zero.

New opt-in reports use version 3. Include each case's byte value/status, source,
capture point, observer module SHA-256, successful count and overall status. Populate the existing
environment.cli_peak_rss_bytes only when all six memory observations are available;
otherwise keep it null, with any partial observed maximum separately labeled.
No flag means no extra child observation pass and unchanged version 1/2 behavior.

This measures OS-reported process-lifetime peaks for six separate observation
processes, not memory of the timed processes themselves, current usage, combined
concurrent memory, an allocation delta or leak evidence. The extra pass is outside
latency samples. Record cache/background-load and platform limitations. Historical
reports remain unchanged. No schema/domain API/production CLI/dependency change.

## Consequences and evidence

Use only generated fixtures, bounded six calls and 300-second per-call deadlines.
No contents, credentials or background monitoring. Verify an allocating child
against its own OS high-water reading, release its buffer before exit, and require
the retained-handle result to preserve that peak. Add timeout/nonzero-exit tests and
keep existing report/oracle/no-overwrite tests. Local execution proves Windows only;
Linux/unsupported fallback execution awaits CI. Core measurements and CLI values
remain separate; no SPEC latency/memory acceptance or speedup is inferred.

## References

- [Microsoft GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo)
- [Python subprocess](https://docs.python.org/3/library/subprocess.html)
