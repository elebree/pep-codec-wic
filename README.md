# PEP WIC Codec

PEP WIC Codec lets Windows understand `.pep` image files. After installation,
Windows Explorer and other programs that use Windows Imaging Component can
recognize PEP images, show them as image files, and use the built-in Windows
thumbnail and preview system for them.

Use this if you have `.pep` files and want Windows to handle them more like
PNG, JPEG, GIF, or other image formats. It does not replace an image editor and
it does not convert files by itself; it adds Windows-level read support for the
PEP image format.

## Attribution

The PEP format and original encoder/decoder implementation come from
[ENDESGA/PEP](https://github.com/ENDESGA/PEP). Credit for that work belongs to
ENDESGA and the upstream repository's respective authors and contributors.

## Build

Run from a Visual Studio Developer PowerShell:

```powershell
cmake -S .\wic -B .\wic\build -G "Visual Studio 17 2022" -A x64
cmake --build .\wic\build --config Release
```

The DLL is written to `wic\build\Release\PepWicCodec.dll`.

## Register

WIC codec discovery uses machine-wide codec registration on this system. The
script will request elevation through UAC if it is not already running as
Administrator:

```powershell
.\wic\tools\register.ps1 .\wic\build\Release\PepWicCodec.dll
```

When no DLL path is passed, the script first looks for `PepWicCodec.dll` in the
same folder as the script. This is the default layout in the release archive, so
installing from an extracted release can use:

```powershell
.\register.ps1
```

The registration also marks `.pep` as an image file type and attaches Windows'
built-in photo thumbnail provider. If File Explorer was already open, restart
Explorer or sign out/in before checking thumbnails or the preview pane.

To remove it:

```powershell
.\wic\tools\unregister.ps1 .\wic\build\Release\PepWicCodec.dll
```

From an extracted release archive, run:

```powershell
.\unregister.ps1
```
