# Project dependency overlays

The poppler integration is derived from microsoft/vcpkg port poppler at baseline
`ec48c11eae9448c8c6e7739d93e06fa2433aa95f`. Its integration files retain vcpkg's
MIT license (VCPKG-LICENSE.txt); Poppler itself is GPL-2.0-or-later, accepted for
this milestone. No FileWeaver project license is selected by this overlay.

Use the official Poppler 26.11.0 archive and fixed SHA-512 in portfile.cmake,
rather than changing the baseline/Qt or using the older baseline Poppler version.
The baseline export/private-namespace patches integrate CMake targets. C++/Splash
rendering is enabled, while Qt bindings, utilities, curl, signing, font subsetting
and optional Brotli PDF streams are disabled. Standard zlib PDF streams remain.
Windows uses the operating system font lookup, which can read installed fonts.
Only the desktop manifest feature selects this package. No runtime downloads.

Future distribution must review Poppler and transitive license obligations; this
milestone creates no release package. Do not replace fixed hashes with bypasses.
