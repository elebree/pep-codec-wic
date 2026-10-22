# PEP WIC Codec

[![Build](https://github.com/elebree/pep-wic-codec/actions/workflows/release.yml/badge.svg?branch=main)](https://github.com/elebree/pep-wic-codec/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/elebree/pep-wic-codec)](https://github.com/elebree/pep-wic-codec/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/elebree/pep-wic-codec/total)](https://github.com/elebree/pep-wic-codec/releases)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6)
[![License: CC0-1.0](https://img.shields.io/badge/license-CC0--1.0-lightgrey)](LICENSE)

PEP WIC Codec lets Windows understand `.pep` image files. After installation,
Windows Explorer and other programs that use Windows Imaging Component can
recognize PEP images, show them as image files, and use the built-in Windows
thumbnail, preview, details pane, and image property system for them.

Use this if you have `.pep` files and want Windows to handle them more like
PNG, JPEG, GIF, or other image formats. It does not replace an image editor, and
the codec does not convert files by itself: it adds Windows-level read support
for the PEP image format. For converting, the installer can also add `pep`, a
command-line tool that encodes images to PEP and decodes PEP files to PNG, BMP
or TIFF.

## Install

Download `PepWicCodecSetup-win-x64.exe` from the
[latest GitHub release](https://github.com/elebree/pep-wic-codec/releases/latest)
and run it.

The installer offers two components:

- **WIC codec** (selected by default): thumbnails, preview, and image
  properties for `.pep` files in File Explorer and other Windows programs.
- **Command-line tool** (not selected by default): `pep.exe`, added to `PATH`.
  In a new terminal, run `pep encode image.png image.pep`,
  `pep decode image.pep image.png`, or `pep info *.pep`; `pep --help` lists
  the options.

To change the selection later, run the installer again.

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
