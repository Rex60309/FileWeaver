# ADR-0040: Windows result layout, type filters and oriented previews

Date: 2026-11-12
Status: Accepted user-requested scope; local Windows GCC builds/tests/format/lint passed; hosted CI pending.

## Context and acceptance

The user requests more visible files, right-side checkbox type filters and a fix
for small/flipped previews with wasted blank space. This is Windows Phase 2.
Accept when compact controls increase result height; the visible result limit
can be changed up to 1000; checkbox categories are combined as OR before global
sorting/limits; image and text areas are mutually exclusive; JPEG EXIF rotation
and mirroring are respected and image display preserves aspect ratio on resize.
Relevant modules: shared SearchFilters/SQLite adapter, SearchWindow, Qt image
renderer and generated-fixture integration/desktop regression tests.

## Decision

Extend the C++ SearchFilters with a trailing optional-by-empty extensions vector
(maximum 64). Normalize like the existing extension filter; parameter-bind every
value. OR across this vector, AND with all existing filters including single ext.
Empty vector means all types. CLI flags and DB schema remain unchanged. Type
categories are explicit suffix lists with tooltips; they describe metadata only,
not MIME recognition or additional preview support. This avoids filtering only an
already-limited UI page or separately merging/ranking several search results.

Keep GUI default 100 and maximum 1000; expose the control directly in the sidebar.
Provide a compact-settings toggle to reclaim vertical room without disabling any
active hotkey/watch/preference capability. Keep its state transient.
Use the same preview slot for image/text, resize images to fit with aspect ratio.
Keep decoded thumbnails at 256x256 and existing IPC/cache/input/time/memory caps;
UI upscaling improves usable area but cannot create source-resolution detail.
Apply Qt image-reader EXIF transformations inside the isolated parser, then bound
the transformed result to 256x256 without forcing the pre-rotation aspect ratio.
No new dependencies, content collection, worker, schema or source-file writes.

## Verification and limitations

Generated EXIF orientation-6 fixture failed before correction (128 high instead
of expected 512 relative to width); rotation, mirror, pane switching, bounded
query and SQL injection/regression tests accompany changes. Run repository core
and desktop Debug/Release builds/tests, format and lint, and inspect final diff.
Hosted MSVC and supported-core sanitizers are separate acceptance evidence;
Windows GUI combined ASan/UBSan is unsupported locally. Tests never use real data.
No pagination, unlimited retrieval, source-resolution preview, new formats,
Linux desktop work, release packaging or Phase 3 features are included.

## Follow-up: horizontal toolbar and content-only pane

2026-11-12: user requests removing the generic preview notice and moving type
checkboxes into one horizontal row, keeping the right side for content. Types
and the existing result limit move above the splitter; a disabled preview hides
the right pane entirely. Normal text/image success has an empty notice; empty
notice labels are hidden. Errors, loading, truncation and GIF/PDF-specific labels
remain visible. No file-reading policy, decoder cap, schema/dependency/API or
threading change. Existing layout/image tests additionally verify horizontal
checkbox alignment and absence of the generic notice. Local Windows Debug/Release
builds/tests, core/desktop format and lint passed; hosted checks remain pending.
