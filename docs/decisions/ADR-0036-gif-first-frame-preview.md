# ADR-0036: Bounded GIF first-frame preview
Date: 2026-11-12
Status: Accepted by explicit user confirmation.

## Context and decision
The authorized next milestone is Windows GIF first-frame preview only. Reuse the
existing opt-in helper, bounded latest-request worker, image limits, cancellation,
and SHA-256-validated memory cache. The pinned Qt installation already includes
QGifPlugin; deploy it alongside QJpegPlugin with no manifest/package/schema change.

Before decoding, validate the logical canvas and first image descriptor against
the same dimension/pixel caps; Qt can clip oversized declared images when reporting
size. Skip only bounded extension sub-blocks and the bounded global color table,
without interpreting payloads or traversing later frames. Reject truncated headers,
missing first frames and first frames extending outside the canvas.

Select the GIF decoder explicitly by suffix and call read exactly once. Do not
query frame counts, iterate frames, construct QMovie, or play animation. All GIFs,
including static ones, display a first-frame/no-animation label. Add an optional
boolean first_frame to the internal bounded image/cache reply; reject wrong types.
The helper includes it on cache hits, so the parent cannot lose the label when
reusing validated pixels. Existing replies without the flag remain compatible.

Limits remain 8 MiB compressed bytes, 8192 per dimension, 16,777,216 pixels,
64 MiB decoder allocation, 192 MiB process private memory, 2 s user CPU, 5 s wall,
512 KiB IPC and 256x256 thumbnails. Existing process isolation is not a complete
permissions sandbox. Files stay read-only; activation remains off every launch.

## Alternatives and consequences
Full animation would require bounded frame/timing/cache semantics and adds work
outside this milestone. A new image library would add an unnecessary dependency.
Using the existing Qt decoder keeps deployment narrow; corrupt GIF decoder failures
remain isolated by the same limits. Additional decoder attack surface is confined
to explicitly opted-in previews, not automatic scanning or parent image decoding.

## Verification and exclusions
Generated static/two-frame GIFs verify the first image, source bytes, clear label,
cache hits and checksum invalidation; corrupt/oversized/dimension/cancellation paths
are tested. Run repository format/lint, core and desktop Debug/Release gates and
hosted CI. Update README/feature status with actual evidence. No PDF, animation,
new network behavior, database migrations or Phase 3-5 functionality.

The preceding successful desktop CI required approximately 88 minutes, leaving
little margin under its 90-minute job cap. Raise only that job cap to 120 minutes
for dependency-build variability; preserve all build/test steps and helper runtime
limits. This is a CI time allowance, not a preview resource-limit relaxation.
