#ifndef AppVersion
#define AppVersion "0.8.0"
#endif

#ifndef SourceDir
#define SourceDir "..\wic\build\Release"
#endif

#ifndef CliSourceDir
#define CliSourceDir "..\cli\build\Release"
#endif

#ifndef OutputDir
#define OutputDir "..\artifacts"
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
ChangesEnvironment=yes
LicenseFile=..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=PepWicCodecSetup-win-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
UninstallDisplayName=PEP WIC Codec
UninstallDisplayIcon={app}\PepWicCodec.dll

; A single custom type shows the components as a plain checklist. Components
; listed with "Types: custom" start checked.
[Types]
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "codec"; Description: "WIC codec: .pep thumbnails, preview and properties in File Explorer"; Types: custom
Name: "cli"; Description: "Command-line tool: pep.exe to encode and decode .pep files, added to PATH"

[Files]
Source: "{#SourceDir}\PepWicCodec.dll"; DestDir: "{app}"; Components: codec; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit
Source: "{#CliSourceDir}\pep.exe"; DestDir: "{app}\bin"; Components: cli; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\PEP WIC Codec README"; Filename: "{app}\README.md"
Name: "{group}\Uninstall PEP WIC Codec"; Filename: "{uninstallexe}"

[Messages]
FinishedLabel=Setup has finished installing [name] on your computer.%n%nIf you installed the WIC codec and File Explorer was already open, restart Explorer or sign out/in before checking thumbnails, image properties, and the preview pane.%n%nIf you installed the command-line tool, open a new terminal to use pep.
FinishedLabelNoIcons=Setup has finished installing [name] on your computer.%n%nIf you installed the WIC codec and File Explorer was already open, restart Explorer or sign out/in before checking thumbnails, image properties, and the preview pane.%n%nIf you installed the command-line tool, open a new terminal to use pep.

[CustomMessages]
NoComponentsSelected=Select at least one component to install.

[Run]
Filename: "{cmd}"; Parameters: "/C ""taskkill /F /IM explorer.exe >NUL 2>&1 & start explorer.exe"""; Description: "Restart File Explorer now"; Components: codec; Flags: runhidden postinstall unchecked skipifsilent

[Code]
const
  EnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function CodecPath: String;
begin
  Result := ExpandConstant('{app}\PepWicCodec.dll');
end;

function CliDir: String;
begin
  Result := ExpandConstant('{app}\bin');
end;

function SameDir(const A, B: String): Boolean;
begin
  Result := CompareText(RemoveBackslash(RemoveQuotes(Trim(A))), RemoveBackslash(RemoveQuotes(Trim(B)))) = 0;
end;

// Returns Path without the entries naming Dir, keeping every other entry as it was.
function PathWithoutDir(const Path, Dir: String; var Found: Boolean): String;
var
  Rest, Entry: String;
  Separator: Integer;
begin
  Result := '';
  Found := False;
  Rest := Path + ';';
  while Rest <> '' do
  begin
    Separator := Pos(';', Rest);
    Entry := Copy(Rest, 1, Separator - 1);
    Delete(Rest, 1, Separator);
    if SameDir(Entry, Dir) then
      Found := True
    else
      Result := Result + Entry + ';';
  end;
  // Drop the separator added after the last entry.
  Delete(Result, Length(Result), 1);
end;

procedure AddCliToPath;
var
  Path, Unused: String;
  Found: Boolean;
begin
  // Never write Path without having read it, or the rest of it would be lost.
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Path) then
  begin
    Log('Could not read Path; not adding ' + CliDir);
    Exit;
  end;
  Unused := PathWithoutDir(Path, CliDir, Found);
  if Found then
    Exit;
  if (Path <> '') and (Path[Length(Path)] <> ';') then
    Path := Path + ';';
  if not RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Path + CliDir) then
    Log('Could not add ' + CliDir + ' to Path');
end;

procedure RemoveCliFromPath;
var
  Path: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Path) then
    Exit;
  Path := PathWithoutDir(Path, CliDir, Found);
  if Found and not RegWriteExpandStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Path) then
    Log('Could not remove ' + CliDir + ' from Path');
end;

// Setup doesn't remove components that were installed before and are now deselected.
procedure RemoveDeselectedComponents;
var
  Codec: String;
begin
  Codec := CodecPath;
  if not WizardIsComponentSelected('codec') and FileExists(Codec) then
  begin
    try
      UnregisterServer(True, Codec, False);
    except
      Log('Could not unregister ' + Codec + ': ' + GetExceptionMessage);
    end;
    // Explorer and other WIC consumers may still have it loaded; then delete it on restart.
    if not DeleteFile(Codec) then
    try
      RestartReplace(Codec, '');
    except
      Log('Could not delete ' + Codec + ': ' + GetExceptionMessage);
    end;
  end;

  if not WizardIsComponentSelected('cli') then
  begin
    RemoveCliFromPath;
    DeleteFile(CliDir + '\pep.exe');
    RemoveDir(CliDir);
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and (WizardSelectedComponents(False) = '') then
  begin
    MsgBox(CustomMessage('NoComponentsSelected'), mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    RemoveDeselectedComponents
  else if (CurStep = ssPostInstall) and WizardIsComponentSelected('cli') then
    AddCliToPath;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveCliFromPath;
end;
