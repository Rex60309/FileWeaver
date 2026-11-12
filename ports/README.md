# Project dependency overlays

The poppler integration is derived from microsoft/vcpkg port poppler at baseline
`ec48c11eae9448c8c6e7739d93e06fa2433aa95f`. Its integration files retain vcpkg's
MIT license (VCPKG-LICENSE.txt). Poppler itself is GPL-2.0-or-later. This overlay
does not set a license for FileWeaver.

The port uses the official Poppler 26.11.0 archive and the SHA-512 in portfile.cmake.
This keeps the dependency baseline and Qt version unchanged.
The baseline export/private-namespace patches integrate CMake targets. C++/Splash
rendering is enabled, while Qt bindings, utilities, curl, signing, font subsetting
and optional Brotli PDF streams are disabled. Standard zlib PDF streams remain.
Windows uses the operating system font lookup, which can read installed fonts.
Only the desktop manifest feature selects this package. No runtime downloads.

Before distributing a build, check the license requirements for Poppler and its
dependencies. There is no release package yet. Keep checksum verification enabled
when updating dependency downloads.
