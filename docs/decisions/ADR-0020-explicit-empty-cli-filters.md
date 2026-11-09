# ADR-0020: Preserve explicit empty CLI text filters

Date: 2026-11-09
Status: Accepted bug fix restoring the existing Phase 1 filter contract

## Context and alternatives

The generated matrix exposed a CLI-only defect. CLI11 conversion into
optional<string> resets an explicitly supplied empty string. --ext followed by an
empty argument returned both extensionless and .txt files; --root/modified-after/
modified-before with an empty argument silently omitted the constraint. Numeric
empty bounds already failed validation. Domain optional-empty extension behavior
was correct and documented in ADR-0007.

## Decision

Bind text options to ordinary strings and retain their CLI::Option presence.
Only if the option was supplied, copy the value into the application request.
An explicit empty extension stays engaged and matches extensionless records.
Explicit empty root/time strings reach existing domain/UTC validation and return
exit 2. Absence remains no constraint. Do not duplicate matching/validation in
CLI callbacks or alter domain types, SQL, schema, exit codes or JSON structure.

Preserve required-value/missing-option and dash-prefixed-literal behavior. Do not
rewrite argv tokens, reinterpret user paths, or change CLI11's equals-only empty
syntax. Document that an empty value must be a separate preserved argument;
--ext= is not the supported spelling of that value in the current parser.

## Evidence and consequences

A two-file regression failed before the fix: --ext plus an empty argument returned
data and data.txt instead of only data. Direct probes also reproduced silently
omitted empty root/time constraints. Add tests requiring empty root/time/numeric
values to fail with the existing single-document JSON error contract. Keep all
normal, missing-value, attached dash-prefixed value and generated matrix checks.
No user files are changed; tests use owned temporaries. No third-party dependency,
network behavior, database migration or public matching-rule change is introduced.
