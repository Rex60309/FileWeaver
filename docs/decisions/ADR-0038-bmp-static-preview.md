# ADR-0038: Opt-in Windows BMP static preview

Date: 2026-11-12
Status: Accepted by explicit user confirmation; all local gates passed; hosted verification pending.

## Context and alternatives

The user selected continuing Windows Phase 2, with Linux deferred, and explicitly
approved BMP static thumbnails using existing Qt without additional packages.
Keeping all additional image formats deferred would leave this selected preview
slice unavailable. Adding another decoder/package is unnecessary for BMP.

## Decision

Recognize only .bmp (case-insensitive) and select Qt's built-in BMP handler
explicitly. Reuse the existing local-file guards, bounded memory read, isolated
helper, latest-request cancellation and SHA-256-validated memory-only cache.
Input <=8 MiB; dimensions <=8192 and <=16,777,216 pixels; output <=256x256.
Existing 192 MiB private-memory, 2 s user-CPU, 5 s wall and 512 KiB IPC limits
remain. No format autodetection, DIB/ICO support, extra plugin, dependency, schema,
source mutation, automatic external launch or activation persistence.

Qt's BMP size reports signed height for top-down images. Normalize only bounded
negative heights before the common dimension check; never negate INT_MIN or
accept larger declared dimensions. The decoder still validates the actual data.
Use existing image conversion and aspect-preserving scaling, not a custom BMP
pixel decoder. Malformed/truncated/over-limit inputs fail with ordinary preview
errors. Unsupported BMP variants are allowed to fail; no universal BMP claim.

## Evidence and consequences

Generated BMPs exercise Unicode/uppercase extension, pixel colors/aspect ratio,
top-down storage, unchanged source, cache hit/invalidation, oversized dimensions
and pixel counts, truncation, input cap and cancellation. Run focused then full
desktop Debug/Release and format/lint gates; record results in the preview ledger.
BMP adds native parser exposure within the same resource isolation, which is
not an OS permissions sandbox. No runtime network or persistent content storage.

During PDF integration acceptance, a failing GUI regression showed QWidget::hide
could leave preview enabled and its image retained. Handling hideEvent restores
the already documented lifecycle for all formats; no opt-in policy is expanded.
The regression must verify clearing and explicit re-enabling with fresh content.
