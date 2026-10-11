; Inno Setup script for the 7-Zip (custom build) exe installer. Driven by
; scripts\create-installer.ps1 -Exe, which passes the defines below:
;   AppVersion  e.g. 23.01
;   StageDir    folder holding 7zFM.exe, 7zG.exe, 7z.exe, 7z.dll, 7-zip.dll, License.txt, readme.txt
;   OutputDir   where the setup exe is written
;   RepoRoot    repository root (for the icon)
;   Arch        x64 or arm64
; Installs per-user by default (no UAC prompt); the "all users" choice in the
; privileges dialog installs to Program Files and registers in HKLM instead.
; Components: gui (7zFM, 7zG), cli (7z.exe + PATH), shell (Explorer context menu).
; Same layout and registry entries as scripts\install.ps1.

#ifndef AppVersion
  #error AppVersion must be defined (use scripts\create-installer.ps1 -Exe)
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#define ShellClsid "{{23170F69-40C1-278A-1000-000100020000}"

[Setup]
AppId={{B6E2D7A4-2C1F-4F58-9C3E-7A1D0E5F9B21}
AppName=7-Zip (custom build)
AppVersion={#AppVersion}
AppVerName=7-Zip (custom build) {#AppVersion}
AppPublisher=7-Zip (custom build)
AppComments=7-Zip file archiver with File Manager themes
DefaultDirName={autopf}\7-Zip
DefaultGroupName=7-Zip
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
MinVersion=6.1
LicenseFile={#StageDir}\License.txt
SetupIconFile={#RepoRoot}\CPP\7zip\UI\FileManager\FM.ico
UninstallDisplayIcon={app}\7zFM.exe
UninstallDisplayName=7-Zip (custom build)
OutputDir={#OutputDir}
OutputBaseFilename=7-Zip-{#AppVersion}-win-{#Arch}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes
ChangesAssociations=yes
VersionInfoVersion={#AppVersion}
VersionInfoProductName=7-Zip (custom build)

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "gui"; Description: "File Manager (7zFM.exe) and GUI (7zG.exe)"; Types: full custom
Name: "cli"; Description: "Command line (7z.exe), added to PATH"; Types: full custom
Name: "shell"; Description: "Explorer context menu (7-zip.dll)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: gui; Flags: unchecked

[Files]
Source: "{#StageDir}\7zFM.exe"; DestDir: "{app}"; Components: gui; Flags: ignoreversion
Source: "{#StageDir}\7zG.exe"; DestDir: "{app}"; Components: gui shell; Flags: ignoreversion
Source: "{#StageDir}\7z.exe"; DestDir: "{app}"; Components: cli; Flags: ignoreversion
Source: "{#StageDir}\7z.dll"; DestDir: "{app}"; Flags: ignoreversion
; Explorer keeps the shell extension loaded: replace / delete it on restart if it's in use.
Source: "{#StageDir}\7-zip.dll"; DestDir: "{app}"; Components: shell; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StageDir}\License.txt"; DestDir: "{app}"
Source: "{#StageDir}\readme.txt"; DestDir: "{app}"

[Icons]
Name: "{autoprograms}\7-Zip File Manager"; Filename: "{app}\7zFM.exe"; Components: gui
Name: "{autodesktop}\7-Zip File Manager"; Filename: "{app}\7zFM.exe"; Tasks: desktopicon

[Registry]
; HKA = HKLM for an all-users install, HKCU for a per-user install.
; Parent keys first: uninstall processes entries in reverse order, so they are
; deleted after our handler keys, and only if nothing else is in them.
Root: HKA; Subkey: "Software\Classes\*\shellex"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\*\shellex\ContextMenuHandlers"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Folder\shellex"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Folder\shellex\ContextMenuHandlers"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Directory\shellex"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Directory\shellex\ContextMenuHandlers"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Directory\shellex\DragDropHandlers"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Drive\shellex"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Drive\shellex\DragDropHandlers"; Components: shell; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\CLSID\{#ShellClsid}"; ValueType: string; ValueName: ""; ValueData: "7-Zip Shell Extension"; Components: shell; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\CLSID\{#ShellClsid}\InprocServer32"; ValueType: string; ValueName: ""; ValueData: "{app}\7-zip.dll"; Components: shell
Root: HKA; Subkey: "Software\Classes\CLSID\{#ShellClsid}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Components: shell
Root: HKA; Subkey: "Software\Classes\*\shellex\ContextMenuHandlers\7-Zip"; ValueType: string; ValueName: ""; ValueData: "{#ShellClsid}"; Components: shell; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Folder\shellex\ContextMenuHandlers\7-Zip"; ValueType: string; ValueName: ""; ValueData: "{#ShellClsid}"; Components: shell; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Directory\shellex\ContextMenuHandlers\7-Zip"; ValueType: string; ValueName: ""; ValueData: "{#ShellClsid}"; Components: shell; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Directory\shellex\DragDropHandlers\7-Zip"; ValueType: string; ValueName: ""; ValueData: "{#ShellClsid}"; Components: shell; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Drive\shellex\DragDropHandlers\7-Zip"; ValueType: string; ValueName: ""; ValueData: "{#ShellClsid}"; Components: shell; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved"; ValueType: string; ValueName: "{#ShellClsid}"; ValueData: "7-Zip Shell Extension"; Components: shell; Flags: uninsdeletevalue uninsdeletekeyifempty

[Run]
Filename: "{app}\7zFM.exe"; Description: "{cm:LaunchProgram,7-Zip File Manager}"; Components: gui; Flags: nowait postinstall skipifsilent

[Code]
const
  MachineEnvKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function EnvRoot: Integer;
begin
  if IsAdminInstallMode then Result := HKLM else Result := HKCU;
end;

function EnvKey: String;
begin
  if IsAdminInstallMode then Result := MachineEnvKey else Result := 'Environment';
end;

function SamePath(A, B: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(A), RemoveBackslashUnlessRoot(B)) = 0;
end;

// Returns PATH without (Dir); Found = Dir was in PATH.
function PathWithout(P, Dir: String; var Found: Boolean): String;
var
  I: Integer;
  Part: String;
begin
  Result := '';
  Found := False;
  P := P + ';';
  while P <> '' do
  begin
    I := Pos(';', P);
    Part := Copy(P, 1, I - 1);
    Delete(P, 1, I);
    if Part = '' then
      continue;
    if SamePath(Part, Dir) then
      Found := True
    else
    begin
      if Result <> '' then Result := Result + ';';
      Result := Result + Part;
    end;
  end;
end;

procedure AddToPath(Dir: String);
var
  P, Rest: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', P) then P := '';
  Rest := PathWithout(P, Dir, Found);
  if Found then exit;
  if Rest <> '' then Rest := Rest + ';';
  RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', Rest + Dir);
end;

procedure RemoveFromPath(Dir: String);
var
  P, Rest: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', P) then exit;
  Rest := PathWithout(P, Dir, Found);
  if Found then RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', Rest);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Existing: String;
begin
  Result := True;
  // don't overwrite an official 7-Zip installation
  if (CurPageID = wpSelectDir) and FileExists(AddBackslash(WizardDirValue) + 'Uninstall.exe') then
  begin
    MsgBox('This folder has another 7-Zip installation (official installer).' + #13#10 +
      'Choose another folder.', mbError, MB_OK);
    Result := False;
  end;
  if (CurPageID = wpSelectComponents) and WizardIsComponentSelected('shell') then
    if RegQueryStringValue(HKA, 'Software\Classes\CLSID\{#ShellClsid}\InprocServer32', '', Existing) then
      if (Existing <> '') and not SamePath(ExtractFileDir(Existing), WizardDirValue) and IsAdminInstallMode then
        Result := MsgBox('The Explorer context menu of another 7-Zip is registered for all users:' + #13#10 +
          Existing + #13#10#13#10 + 'This installation replaces it. Uninstalling removes the context menu ' +
          '(repair the other 7-Zip to get it back). Continue?', mbConfirmation, MB_YESNO) = IDYES;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsComponentSelected('cli') then
      AddToPath(ExpandConstant('{app}'))
    else
      RemoveFromPath(ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
