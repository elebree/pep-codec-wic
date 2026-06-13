# Build and Packaging

## Build

Run from a Visual Studio Developer PowerShell:

```powershell
cmake -S .\wic -B .\wic\build -G "Visual Studio 17 2022" -A x64
cmake --build .\wic\build --config Release
```

The DLL is written to `wic\build\Release\PepWicCodec.dll`.

## Installer

After building the Release DLL, install Inno Setup 6 and run:

```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" .\wic\installer\PepWicCodec.iss
```

The installer is written to `artifacts\PepWicCodecSetup-win-x64.exe`. It
requires administrator approval, installs the codec under Program Files, and
registers/unregisters the WIC COM server during install and uninstall. At the
end of installation, it offers an optional File Explorer restart so thumbnails
and preview handlers can pick up the new codec immediately.

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
