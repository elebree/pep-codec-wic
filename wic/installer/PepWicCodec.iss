#ifndef AppVersion
#define AppVersion "0.6.0"
#endif

#ifndef SourceDir
#define SourceDir "..\build\Release"
#endif

#ifndef OutputDir
#define OutputDir "..\..\artifacts"
#endif

[Setup]
AppId={{F5C347B6-8502-4FA5-8B35-875BBD966C63}
AppName=PEP WIC Codec
AppVersion={#AppVersion}
AppPublisher=elebree
AppPublisherURL=https://github.com/elebree/pep-wic-codec
AppSupportURL=https://github.com/elebree/pep-wic-codec/issues
AppUpdatesURL=https://github.com/elebree/pep-wic-codec/releases
DefaultDirName={commonpf}\PEP WIC Codec
DefaultGroupName=PEP WIC Codec
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
ChangesAssociations=yes
LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=PepWicCodecSetup-win-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
UninstallDisplayName=PEP WIC Codec
UninstallDisplayIcon={app}\PepWicCodec.dll

[Files]
Source: "{#SourceDir}\PepWicCodec.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\PEP WIC Codec README"; Filename: "{app}\README.md"
Name: "{group}\Uninstall PEP WIC Codec"; Filename: "{uninstallexe}"

[Messages]
FinishedLabel=Setup has finished installing [name] on your computer.%n%nIf File Explorer was already open, restart Explorer or sign out/in before checking thumbnails and the preview pane.
FinishedLabelNoIcons=Setup has finished installing [name] on your computer.%n%nIf File Explorer was already open, restart Explorer or sign out/in before checking thumbnails and the preview pane.

[Run]
Filename: "{cmd}"; Parameters: "/C ""taskkill /F /IM explorer.exe >NUL 2>&1 & start explorer.exe"""; Description: "Restart File Explorer now"; Flags: runhidden postinstall unchecked skipifsilent
