# ADR-0002: Explicit SQLite initialization

Date: 2026-11-08
Status: Accepted under full implementation authorization

Schema version superseded by ADR-0004: current initialization creates/upgrades
to v2. The v1 decision below is retained as migration history.

## Context and alternatives

The foundation needs persistent storage without implicitly scanning roots.
Alternatives are implicit per-user storage, an explicit database argument, or
in-memory storage only. Choose an explicit path; no default home-directory write.

## Decision

`fileweaver init --database <path>` opens or creates only the specified database.
Its parent must exist. SQLite receives UTF-8 paths, enables foreign keys and WAL,
and uses a five-second bounded busy timeout. Each call owns one connection on its
calling thread. RAII closes connections and statements on all failure paths.

Schema version 1 contains only `settings(key TEXT PRIMARY KEY, value TEXT NOT
NULL)`. Creation and user_version update share one immediate transaction. Version
0 databases containing user tables are rejected; this command must not adopt an
unrelated database. Unknown newer versions are rejected without migration.
No roots/files schema is introduced until its incremental-index tests exist.
Existing version 1 databases must contain the expected settings columns.

Errors use SPEC exit codes: argument 2, open/permission 3, schema 5. SQL literals
are internal constants; no input is concatenated into SQL. JSON output is deferred
until the CLI output contract slice, and is not advertised as available.

## Consequences

Initialization can create SQLite database/WAL/SHM files at the explicit path.
Repeated initialization is idempotent. Schema failures roll back; corruption
and unsupported versions are reported. Synthetic temporary fixtures test fresh
creation, idempotence, foreign versions, corruption and transactional rollback.
Future schema versions require new transactional migrations and tests.
