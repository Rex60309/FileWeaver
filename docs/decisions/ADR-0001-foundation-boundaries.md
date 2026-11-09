# ADR-0001: Foundation boundaries

Date: 2026-11-08
Status: Accepted under user authorization for full development and dependencies

## Context

The repository begins with the v2 specification and development policy only.
Implementation must proceed in verified slices, preserve C++20 portability, and
keep presentation separate from storage and filesystem behavior.

## Alternatives

Implement all modules together; build a Qt-dependent core; or introduce one
independently verified application capability at a time.

## Decision

Use CMake/Ninja with strict warnings and explicit sanitizer support checks.
Start with a help-only CLI contract. The next slice uses the specification's
CLI11, SQLite and Catch2 through pinned vcpkg, rather than custom equivalents.
Before persistent storage is introduced, record its transactional schema and
error mapping in a separate ADR and test fresh creation, upgrades and rollback.
SQLite connections will remain confined to their owning execution context.
No background threads or GUI dependencies are introduced in the foundation.

The application layer will own use cases. Domain interfaces will use standard
C++ types; SQLite and OS implementation details stay in adapters. Network access
is limited to explicitly authorized development dependency installation; the
product has no network behavior by default.

## Consequences

External tooling is required for complete quality gates. A missing tool fails
the corresponding gate; it must never silently skip analysis. CI configuration
is evidence of intended checks, not evidence that those checks have executed.
Later phases require their own implementation, tests and safety decisions.
