# ADR-0018: Generated nested Unicode benchmark profile

Date: 2026-11-08
Status: Accepted under authorization to continue Phase 1 measurements

## Context and alternatives

The flat ASCII fixture does not exercise multilingual paths, repeated basenames
or path tie-breaks. Add a bounded generated profile to the existing developer
runner; do not benchmark real user roots or change production search/storage.

## Decision

--dataset defaults to flat-ascii, preserving old fixture/cases/method labels.
Opt-in nested-unicode generates zero-byte .txt files whose basenames combine
Chinese, Japanese, accent and emoji. Each consecutive group of three indices has
one shared name, placed in three distinct branches and one of eight second-level
directories. At most 27 directories excluding the source root, with exactly two
directory levels. Root and database names also contain Unicode/spaces/emoji.

Use exact first/last/absent cases and substring all-name Japanese token, category
and absent cases. Return at most 100 results. Since 100 is not divisible by three,
the limit cuts through a same-name group; full ordered path comparison must choose
the correct member. Python oracle selects the smallest (name, path, size) tuples;
native oracle independently enumerates the fixture formula in the same binary
name/path order. Both remain outside core timing. Substring oracle folds ASCII
bytes only, matching the existing SQLite contract, without Unicode normalization.

Nested filesystem baseline uses recursive Python os.walk, raises enumeration
errors and is labeled python_recursive_directory_scan. Keep the flat baseline's
old scandir label/behavior. Do not compare different envelopes/profiles as speed
ratios. No links are created or followed in owned synthetic fixtures.

Nested reports use version 4 and record profile, directory/distinct-name counts,
cases, method labels and existing build/hash/hardware/cache/memory metadata. Only
pass --dataset to the native helper for the new profile; old helpers remain usable
with the unchanged default. Native helper self-identifies its fixture profile.
No new dependency, domain API, schema, product CLI flag or source-file mutation.

## Consequences and verification

Test 103 files to exercise a partial final group, duplicate exact counts and the
100-result tie cutoff; include 1-file boundaries and combined native/CLI-memory
options. Reject unknown profiles before fixture/report creation. Preserve all old
flat tests, report exclusive creation and wrong-fixture oracle rejection.
Release measurement records full environment/raw results rather than performance
target compliance. Broader file sizes/types, controlled cold cache, optimized
indexes and supported Linux/sanitizer runtime evidence remain separately scoped.
