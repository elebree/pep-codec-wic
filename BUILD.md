# Build and Packaging

## With mise

[mise](https://mise.jdx.dev) installs the whole toolchain and wraps the steps
below as tasks. Visual Studio is not needed:

```powershell
mise install          # CMake, Ninja, llvm-mingw, Inno Setup
mise run build        # wic\build\Release\PepWicCodec.dll
mise run build-cli    # cli\build\Release\pep.exe, the command-line tool
mise run dist         # installer and zip in artifacts\
mise run register     # also: unregister, restart-explorer, clean
```

`mise run installer 1.2.3` sets the installer version; `mise tasks` lists all
tasks.

The compiler is [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): clang,
lld, libc++ and the mingw-w64 Windows headers. Inno Setup is installed in
portable mode into mise's tool folder, so it doesn't show up in installed apps
and leaves an Inno Setup you already have alone. CI doesn't use mise: it builds
the releases with MSVC and Inno Setup from Chocolatey, because MSVC's binaries
are less than half the size and also run on Windows older than 10.

## Build

With the mise tools on `PATH` (in the project folder with mise activated, or
through `mise exec --`):

```powershell
cmake -S .\wic -B .\wic\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build .\wic\build
```

The DLL is written to `wic\build\Release\PepWicCodec.dll`. It links the C++
runtime statically and needs only the Universal C Runtime, which is part of
Windows 10 and 11.

Both projects also build with MSVC, as CI does, when Visual Studio 2022 with the
C++ workload is installed. CMake's Visual Studio generator finds it, so no
Developer PowerShell is needed:

```powershell
cmake -S .\wic -B .\wic\build -G "Visual Studio 17 2022" -A x64
cmake --build .\wic\build --config Release
```

A build folder keeps the generator it was created with, so run
`mise run clean` before switching between the two.

## Command-line tool

`pep.exe` encodes images to `.pep` and decodes `.pep` files to other formats.
It does not need the codec to be installed. It is a separate CMake project in
`cli\`. The installer offers it as an optional component; it is not in the
release zip:

```powershell
cmake -S .\cli -B .\cli\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build .\cli\build
```

The tool is written to `cli\build\Release\pep.exe`:

```powershell
pep encode sprite.png sprite.pep            # --bits 1|2|4|8 quantizes the palette
pep decode sprite.pep sprite.png            # or .bmp, .tif
pep info *.pep                              # size, colors, palette, model
```

- **encode** reads any image Windows can decode, such as PNG, BMP, GIF or TIFF,
  and takes the first frame of an animated one. An image with more than 256
  colors is reduced to 256, with a warning. Fully transparent pixels first
  become one color, which is sometimes enough and changes nothing visible.
  Otherwise median cut and a few rounds of k-means pick the palette, without
  dithering. It decodes the result before writing it, so it never writes a file
  that doesn't read back.
- A PEP header holds at most 4096 pixels per side. **encode** also saves larger
  images, up to 65535 per side and 64 Mi pixels in all, with a warning. Their
  header shows the size without its high bits, as earlier encoders wrote it, for
  example 2688x464 for 6784x464. `pep decode`, `pep info` and the WIC codec
  find the real size again. Other PEP readers see the wrong size, and decoders
  without the fixes in FIXES.md can hang on such files.
- **decode** writes PNG, BMP or TIFF with alpha. Like the codec, it also reads
  files whose header lost the high bits of a size over 4096, and prints a
  warning when it has to.

## Installer

After building the Release DLL and the command-line tool, run the Inno Setup
compiler that mise installs:

```powershell
ISCC.exe .\installer\PepWicCodec.iss
```

A separately installed Inno Setup 6 works too, called by its full path, such as
`& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"`.

The installer is written to `artifacts\PepWicCodecSetup-win-x64.exe`. It
requires administrator approval and installs under Program Files. It has two
components:

- `codec`, selected by default: the DLL, registered as a WIC COM server during
  install and unregistered during uninstall. At the end of installation, setup
  offers an optional File Explorer restart so thumbnails, preview handlers, and
  image properties can pick up the new codec immediately.
- `cli`, not selected by default: `bin\pep.exe`, with `bin` added to the
  machine `PATH`. Uninstalling removes it from `PATH` again.

Running the installer again with a component deselected removes that
component. A silent install takes the components on the command line, for
example `PepWicCodecSetup-win-x64.exe /VERYSILENT /COMPONENTS="codec,cli"`.

## Register

WIC codec discovery uses machine-wide codec registration on this system. The
script will request elevation through UAC if it is not already running as
Administrator:

```powershell
.\installer\register.ps1 .\wic\build\Release\PepWicCodec.dll
```

When no DLL path is passed, the script first looks for `PepWicCodec.dll` in the
same folder as the script. This is the default layout in the release archive, so
installing from an extracted release can use:

```powershell
.\register.ps1
```

The registration also marks `.pep` as an image/picture file type, attaches
Windows' built-in photo thumbnail provider and image property handler, and
registers the image property lists used by Explorer's details pane. If File
Explorer was already open, restart Explorer or sign out/in before checking
thumbnails, image dimensions, details pane values, or the preview pane.

To remove it:

```powershell
.\installer\unregister.ps1 .\wic\build\Release\PepWicCodec.dll
```

From an extracted release archive, run:

```powershell
.\unregister.ps1
```
