# PEP WIC Codec

PEP WIC Codec lets Windows understand `.pep` image files. After installation,
Windows Explorer and other programs that use Windows Imaging Component can
recognize PEP images, show them as image files, and use the built-in Windows
thumbnail and preview system for them.

Use this if you have `.pep` files and want Windows to handle them more like
PNG, JPEG, GIF, or other image formats. It does not replace an image editor and
it does not convert files by itself; it adds Windows-level read support for the
PEP image format.

## Install

Download `PepWicCodecSetup-win-x64.exe` from the
[latest GitHub release](https://github.com/elebree/pep-wic-codec/releases/latest)
and run it.

The installer requires administrator approval because WIC codecs are registered
machine-wide. On the final installer page, you can optionally restart File
Explorer so thumbnails and the preview pane can pick up the codec immediately.
Otherwise, restart Explorer later or sign out and back in.

To remove the codec, uninstall **PEP WIC Codec** from Windows Settings or
Control Panel.

Build, packaging, and manual registration instructions are in
[BUILD.md](BUILD.md).

## Attribution

The PEP format and original encoder/decoder implementation come from
[ENDESGA/PEP](https://github.com/ENDESGA/PEP). Credit for that work belongs to
ENDESGA and the upstream repository's respective authors and contributors.
