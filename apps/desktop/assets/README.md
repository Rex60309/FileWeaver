# FileWeaver app icon

Original geometric document/W mark: navy tile, light folded paper and teal/blue
interlocking strokes. No fonts or downloaded artwork are required.

- fileweaver.svg: editable vector source, 256x256 view box.
- fileweaver.png: 256x256 RGBA raster embedded in Qt resources for window/taskbar use.
- fileweaver.ico: 32-bit PNG frames at 16, 24, 32, 48, 64, 128 and 256 pixels,
  embedded as Windows icon resource 101 for the desktop executable.

Raster exports used Windows System.Drawing, drawing the same vector geometry at
1024x1024 with antialiasing, then high-quality downsampling. ICO directory entries
wrap the exported PNG frames. CMake uses these files directly, so building or
running the app does not require an asset generator or an extra package.
