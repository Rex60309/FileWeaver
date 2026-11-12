# ADR-0037: Opt-in PDF first-page preview with Poppler
Date: 2026-11-12
Status: Accepted by explicit user confirmation; local and hosted Windows gates passed.

## Context and authorization
The user approved Windows PDF first-page static previews using pinned Poppler
(GPL-2.0-or-later), dependency downloads, generated tests, README and full gates.
No source mutation, schema change, password entry, scripting, forms/attachment/link
interaction or later phases are authorized. Preserve manual preview opt-in and
per-launch off defaults. Distribution/license obligations require review before
any future release; no project license is silently selected or changed here.

## Decision
Use Poppler's C++ rendering adapter and Splash, not a browser or document viewer.
Use the maintained official 26.11.0 source archive through a project-local vcpkg
overlay with a fixed SHA-512; retain the existing global vcpkg baseline/Qt version.
Disable unnecessary Qt bindings, utilities, curl/network, signature backends and
optional frontends. Only the desktop dependency feature includes Poppler.

Load bounded source bytes from memory in the existing resource-limited child.
Reject encrypted/locked PDFs without collecting passwords. Create page zero only,
validate finite positive page geometry, render directly at bounded resolution and
return <=256x256 RGBA pixels through existing bounded IPC. No page navigation,
text extraction, embedded file access, document actions or JavaScript execution.
First-page labeling must survive checksum-validated cache hits. Input <=8 MiB;
existing 192 MiB private memory, 2 s user CPU, 5 s wall and 512 KiB IPC remain.
These resource limits are not a full permissions sandbox. Audit the source/API for
external-resource reads rather than assuming memory input implies isolation.

## Alternatives and consequences
Qt PDF is not supported by the current MinGW toolchain; moving toolchains/Qt
packages is a wider decision. PDFium requires a separate build/distribution path.
Poppler provides an existing vcpkg C++ adapter compatible with current builds,
but adds native parser/codec dependencies and GPL distribution obligations.
Keep parser failure isolated and fail closed; do not relax limits for difficult
PDFs. Unsupported/encrypted/over-limit documents retain confirmed external-open
and path-copy options. No automatic external launch.

## Acceptance evidence required
Generated multipage/color PDFs prove only page one is shown and labeled; verify
Unicode, unchanged bytes, cache hits/invalidation, corrupt/empty/encrypted files,
geometry and input caps, scripts/actions not executed, cancellation and stale work.
Run repository core/desktop Debug/Release, format/lint and applicable hosted gates.
README and feature status must distinguish implemented, tested and planned work.

## Implementation details under verification
The GUI/process controller library does not link the PDF renderer. A separate
renderer target is linked only by the helper (and trusted-fixture tests). Diagnostic
callbacks count parser messages without retaining/forwarding document text. Reject
any reported parser diagnostics with a generic count after rendering. Poppler's
memory loader and rendering paths were inspected: stream parsing uses substreams
of the in-memory input; document actions/embedded-file APIs are not invoked. OS
font fallback may read installed system/user fonts; do not claim zero filesystem
access or a permissions sandbox.

Render a fixed 256x256 slice with equal X/Y DPI and white padding. Reject nonfinite,
nonpositive, excessively small page geometry, or coordinates/dimensions exceeding
14,400 points; reject calculated DPI over 7,200. These are preview compatibility
limits, not source edits. SHA-256 validation reads the bounded compressed source;
cache hits avoid parsing/rendering a previously validated identical document.

The overlay keeps only C++/Splash, zlib, required codecs and Windows font lookup.
Optional PDF Brotli, font subsetting, Qt/GLib bindings, tools, curl and signing are
disabled. CMake export integration is adapted to the new upstream source; archive
and patch application are verified before builds. Runtime source data is never
passed as a filesystem filename to Poppler or a shell command.

Poppler 26.11 builds its own implementation with C++23. FileWeaver's CMake target
standard remains C++20; no project source adopts C++23 APIs. The stable public
Poppler C++ headers must compile under the project's existing C++20 flags and
both configured compilers. Do not raise FileWeaver's language standard or weaken
checks to accommodate the dependency. This dependency build detail is recorded
separately from the application stack and is subject to actual build evidence.


Windows checkout integration: dependency patch files require LF even when Git
core.autocrlf is true. The first hosted attempt exposed corrupt empty context
lines after CRLF conversion; .gitattributes now pins only *.patch to LF. A fresh
Windows-converting index checkout and both patch application checks passed.
Archive, patch semantics, baseline and application language standard are unchanged.
