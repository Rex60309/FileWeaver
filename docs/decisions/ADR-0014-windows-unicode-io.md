# ADR-0014: Windows Unicode CLI boundaries

Date: 2026-11-08
Status: Accepted for the authorized Unicode filename bug fix (Phase 1)

## Context and alternatives

The CLI accepted UTF-8 internally but combined CLI11's Unicode argv with the CRT's
narrow argc. A generated emoji-only query reproduced a missing database argument
in human output mode. Windows native consoles also require an explicit Unicode
output boundary; changing the shared console code page would affect the parent.

## Decision

At the presentation boundary, read CommandLineToArgvW with its own count and RAII
LocalFree ownership. Reuse CLI11's UTF-16 to UTF-8 conversion. Pass a stable owned
argument vector and matching count through JSON detection, validation and parsing.
Do not mix arguments from different encodings. POSIX keeps its original argv.

Detect native console handles with GetConsoleMode. Buffer console output per CLI
invocation and convert strict UTF-8 to UTF-16 for bounded WriteConsoleW calls,
without splitting surrogate pairs. Translate newlines for native console output.
Pipes/files keep existing UTF-8 bytes, including the single JSON document contract.
Never change console/system code pages. Conversion/write failures return failure.

## Consequences

No domain API, schema, dependency or source-file mutation changes. The Windows
adapter stays in apps/cli. Tests generate multilingual roots, database names and
filenames, search BMP/non-BMP queries and compare exact UTF-8 JSON/human pipe
output. An isolated hidden native console at CP950 checks BMP Unicode output and
unchanged code page. Emoji screen glyphs depend on host/font and legacy screen
buffers can replace supplementary characters; UTF-8 pipe round trips verify those
characters independently. No Unicode normalization/case-folding expansion is made.
The CLI has no streaming progress; native console output is emitted on completion.

## Follow-up: locating conversion failures

An error reported against a nested multilingual tree could not be reproduced with
valid synthetic names using the same character classes. A malformed native UTF-16
filename did reproduce the identical filesystem conversion exception. This does
not establish the cause in the user's real tree, which was not accessed.
Catch illegal-byte-sequence failures at storage path encoding and report native
code units with ASCII escapes, without attempting another failing conversion.
Preserve the existing failure exit code, generic JSON error contract and complete
metadata rollback. Do not silently replace, skip or rename an unencodable entry,
and do not extend the UTF-8 storage model to WTF-8. No schema/API/safety change.
