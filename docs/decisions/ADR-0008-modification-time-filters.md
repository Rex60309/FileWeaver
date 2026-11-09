# ADR-0008: Explicit UTC modification-time bounds

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1

## Context and alternatives

Schema v2 stores modified_at as signed Unix nanoseconds. Search needs time bounds
without timezone guesses, wall-clock-dependent parsing or unsafe conversions.
Alternatives are locale-dependent dates, raw nanosecond CLI arguments or strict UTC.

## Decision

Expose --modified-after and --modified-before in the existing search command.
Accept exactly YYYY-MM-DDTHH:MM:SSZ, Gregorian year 0001..9999, valid dates and
00..23 hours / 00..59 minutes/seconds. Reject leap-second notation, fractional
seconds, timezone offsets, missing timezone, lowercase separators, relative dates
and embedded NUL. Only whole-second values representable as signed Unix nanoseconds
are accepted: 1677-09-21T00:12:44Z through 2262-04-11T23:47:16Z.

The pure core parser uses C++20 chrono calendar arithmetic, no locale/network/current
clock and no SQLite dependency. Day-to-second arithmetic fits int64 for the fixed
four-digit year range; check seconds against int64 / 1e9 before multiplication.
CLI converts the strings to optional int64 bounds in SearchFilters. C++ callers
can supply nanosecond-resolution int64 values directly, including zero/negative.

Use strict predicates modified_at > after and modified_at < before. Equal or
reversed paired bounds are invalid (exit 2). Single bounds are valid. Combine
with all existing filters before deterministic order and LIMIT. Bind every value;
do not add/subtract one nanosecond around user boundaries. Default absent bounds
preserve prior search behavior and result format. No schema/dependency migration.

## Consequences

Filter stored modification metadata only, not creation/access time or activity.
No re-stat/content reads or timezone inference. Stale index limitations remain.
There is no relative "today", local-date syntax, fractional CLI timestamp or sort
option. Full Unicode/path/other search limitations remain as previously documented.
Tests cover epoch/negative times, Gregorian leap rules, storage endpoints/overflow,
invalid syntax/calendar, strict nanosecond boundaries, full filter combinations,
invalid paired ranges and CLI parsing/error/combined-search contracts.
