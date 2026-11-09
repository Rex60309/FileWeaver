# ADR-0010: Single-document JSON CLI output

Date: 2026-11-08
Status: Accepted under full development/dependency authorization

## Context and alternatives

Automation needs machine-readable stdout with stable exit codes. Alternatives are
mixing human messages with JSON, line-delimited records, a general JSON dependency,
or a small serializer for the fixed primitive output shapes. Choose a fixed-schema
serializer reusing existing string escaping, with independent strict parser tests.

## Decision

Global --json is accepted before/after init/index/search and with help. Detect the
explicit token before parsing so parser failures can also return JSON. Tokens after
the -- delimiter are data; attached values such as --ext=--json do not enable mode.
Reject explicit false flag override. Without --json, preserve human output.

Emit one complete UTF-8 JSON object followed by newline on stdout. Detailed human
diagnostics/warnings stay on stderr. Search serializes its complete bounded result
before any output. Validate UTF-8 (including overlong sequences, surrogate code
points and >U+10FFFF), escape controls/quotes/backslashes, reject invalid text with
runtime exit 1 rather than silently replacing path bytes or emitting partial JSON.
No runtime JSON parsing, schema changes or production dependencies are introduced.
All numbers are decimal JSON integers; consumers must preserve signed 64-bit values.

Success objects use ok=true, exit_code=0 and command=init|index|search|help. Init has
no payload. Help adds text. Search adds results and count; each result contains
file_id, root_id, path, name, size_bytes. Empty search returns results=[] and count=0.
Index adds scan {files,skipped,errors,complete,cancelled}, added, updated, missing,
missing_reconciled. An incomplete index summary uses ok=false, exit_code=1 and
error="Scan incomplete", retaining scan/count fields. A thrown index failure uses
the generic error shape, not misleading partial counts.

Generic errors contain ok=false, exit_code (existing 1..5), and a stable category
error string. Details are stderr only. Error 1 means Runtime failure, 2 Invalid
arguments, 3 Workspace or permission error, 4 Index not initialized, 5 Schema or
metadata error. Main has a final exception boundary returning runtime JSON for
unexpected exceptions. Output-device failure/OOM cannot guarantee delivered JSON.

Add Python >=3.9 as a development/test-only interpreter dependency, discovered by
CMake. Its stdlib json parser strictly validates the entire stdout, duplicate keys,
UTF-8, numeric precision and command/error behavior independently of C++ formatting.
Its sqlite3 module alters only synthetic fixture databases. No pip packages or
Python runtime dependency for the product. Optimized Python is rejected so test
assertions cannot silently disappear.

## Consequences

Consumers should inspect both process exit and ok/exit_code, tolerate extra future
fields, and treat field meanings/types as a public contract. Search order and
filters are unchanged; no query/history collection or source-content access.
Serialization may expand escaped text up to sixfold within existing result caps.
Tests include Unicode/controls/malformed UTF-8, exact int64, success/empty/help,
argument/open/schema/runtime failures, write-failure rollback and warning isolation.
No streaming JSON, JSONL, schema/model migration, performance improvement or CI/
sanitizer execution is implied.
