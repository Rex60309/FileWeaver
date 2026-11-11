# ADR-0034: Opt-in bounded preview helper
Date: 2026-11-11
Status: Accepted by explicit Phase 2 completion confirmation.

Preview defaults off and does not persist activation. After explicit opt-in it
reads only the selected local regular file; no shell/file association, macros,
scripts, Markdown HTML, XML entities or network resources execute. First batch:
TXT/MD/JSON/XML/CSV and source as plain text; PNG/JPEG. PDF/GIF remain second batch.
Existing external-opening allowlist and full-path confirmation remain unchanged.

Use a separate trusted helper executable, one child at a time on a bounded latest-
request worker. Create suspended with an explicit inherited pipe-handle list,
assign Windows job kill-on-close / 192 MiB private memory / 2 s user CPU, then
resume. Parent enforces 5 s monotonic wall deadline, cancellation and 512 KiB IPC.
Fail closed if limits/assignment cannot be established. This is process/resource
isolation, not a restricted-token/filesystem security sandbox. No universal driver
termination guarantee. Do not fall back to parsing untrusted images on GUI thread.

Helper validates local/non-reparse ancestry, opens read-only with reparse-point
protection, validates the opened regular file, reads bounded bytes and checks
metadata before/after for races. These checks are not an atomic filesystem snapshot
or protection against every malicious replacement. Text reads only first 64 KiB,
UTF-8/BOM UTF-16, displays truncation, rejects binary/invalid encodings. Syntax
highlighting is bounded linear text styling, not an interpreter or document parser.
Images are <=8 MiB, <=16M pixels, bounded dimensions/allocation; decoder/OS limits
terminate corrupt/hanging work. Scale to <=256x256. Return validated raw RGBA via
bounded JSON/base64 so the parent never decodes a compressed user image.

Thumbnail cache is memory-only, <=8 entries and <=4 MiB, cleared when preview is
disabled/index changes/close. Helper rereads bounded compressed bytes and SHA-256
before reusing a thumbnail; size/mtime alone are not proof of equal content. A
cache-hash hit skips decoding, not content validation. Text is not cached. No
contents, paths, hashes or thumbnails are written to settings/logs/disk caches.

Enable pinned qtbase PNG/JPEG features (user approved downloads), no PDF/runtime
model/cloud service. Tests use generated fixtures and dedicated test helper for
malformed/oversized output, crash, timeout/cancellation. Test missing/reparse/
unsupported/binary/truncated/Unicode files, corrupt/huge images, cache invalidation,
stale-result rejection and unchanged source bytes. No real user data in tests.

## Context, alternatives and consequences

The first-batch preview must parse untrusted bytes without blocking the Qt thread
or enabling source execution. In-process decoding would share GUI memory/failure
state; a persistent parser service would retain more content and complicate
recovery. A fresh bounded child for each request gives cancellation/crash recovery
at process-startup cost. Native restricted-token/AppContainer isolation was
considered but is not implemented or claimed; resource limits alone do not stop a
compromised parser from exercising the current user's permissions. PNG/JPEG
features expand the existing pinned Qt dependency and its decoder attack surface.
The bounded memory-only cache reduces repeated decoding without trusting mtime.
