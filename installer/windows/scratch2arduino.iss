; Scratch2Arduino - Windows installer (Inno Setup 6).
; Builds installer.exe:  iscc installer\windows\scratch2arduino.iss
;
; The installer ships the sources, then runs build-and-install.ps1, which installs the missing
; prerequisites (CMake, Visual Studio Build Tools, WebView2) and builds Scratch2Arduino with CMake
; directly on the user's machine.
;
; Updating an existing installation is incremental: the sources are unpacked into {app}\staging,
; build-and-install.ps1 replaces only the changed files in {app}\src and recompiles them in the
; build tree kept in {app}\build.

#define AppName "Scratch2Arduino"
#ifndef AppVersion
  #define AppVersion "0.5.0"
#endif
#define SourceRoot "..\.."

[Setup]
AppId={{5640ABA3-0554-4DD2-9C29-429857852E2F}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Scratch2Arduino contributors
AppPublisherURL=https://github.com/Zargith/Scratch2Arduino
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; An update goes into the folder of the existing installation.
UsePreviousAppDir=yes
DisableDirPage=auto
LicenseFile={#SourceRoot}\LICENSE
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#SourceRoot}\resources\icons\scratch2arduino.ico
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\bin\scratch2arduino.exe
ShowLanguageDialog=auto

[Languages]
; The first language is the fallback: French by default.
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
french.Building=Compilation de Scratch2Arduino avec CMake (cela peut prendre plusieurs minutes)...
english.Building=Building Scratch2Arduino with CMake (this can take several minutes)...
french.Updating=Mise à jour de Scratch2Arduino (seules les modifications sont recompilées)...
english.Updating=Updating Scratch2Arduino (only the changes are recompiled)...
french.BuildFailed=La compilation de Scratch2Arduino a échoué.%n%nConsulte le journal :%n%1
english.BuildFailed=Building Scratch2Arduino failed.%n%nSee the log:%n%1
french.UpdateTitle=Mise à jour
english.UpdateTitle=Update
french.UpdateSubtitle=Scratch2Arduino est déjà installé sur cet ordinateur.
english.UpdateSubtitle=Scratch2Arduino is already installed on this computer.
french.UpdateInfo=Scratch2Arduino %1 est déjà installé dans :%n%2%n%nIl sera mis à jour vers la version %3 : les prérequis déjà présents sont conservés et seuls les fichiers modifiés sont recompilés.
english.UpdateInfo=Scratch2Arduino %1 is already installed in:%n%2%n%nIt will be updated to version %3: the prerequisites already present are kept and only the changed files are recompiled.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[InstallDelete]
; Shortcuts of versions before 0.4 (two variants).
Type: files; Name: "{group}\Scratch2Arduino (webview).lnk"
Type: files; Name: "{group}\Scratch2Arduino (native).lnk"
Type: files; Name: "{autodesktop}\Scratch2Arduino (webview).lnk"
Type: files; Name: "{autodesktop}\Scratch2Arduino (native).lnk"
; Leftover of an interrupted update.
Type: filesandordirs; Name: "{app}\staging"

[Files]
Source: "{#SourceRoot}\*"; DestDir: "{app}\staging"; Excludes: "\build,\.git,\.github,\installer\windows\Output,\frontend\node_modules"; Flags: recursesubdirs createallsubdirs ignoreversion
; Must stay last: once every file is copied, AfterInstall runs the CMake build.
Source: "build-and-install.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion; AfterInstall: RunBuild

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\bin\scratch2arduino.exe"; Check: BinaryExists('scratch2arduino.exe')
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bin\scratch2arduino.exe"; Tasks: desktopicon; Check: BinaryExists('scratch2arduino.exe')

[UninstallDelete]
; Files produced by the CMake build are not tracked by Inno Setup.
Type: filesandordirs; Name: "{app}"

[Code]
var
  PreviousVersion, PreviousDir: String;

function UninstallKey(): String;
begin
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{5640ABA3-0554-4DD2-9C29-429857852E2F}_is1';
end;

// Finds an existing installation (any version) from its uninstall entry.
function FindPreviousInstall(): Boolean;
begin
  Result := RegQueryStringValue(HKLM, UninstallKey(), 'InstallLocation', PreviousDir) or
            RegQueryStringValue(HKCU, UninstallKey(), 'InstallLocation', PreviousDir);
  if Result then
  begin
    if not RegQueryStringValue(HKLM, UninstallKey(), 'DisplayVersion', PreviousVersion) then
      RegQueryStringValue(HKCU, UninstallKey(), 'DisplayVersion', PreviousVersion);
    PreviousDir := RemoveBackslashUnlessRoot(PreviousDir);
  end;
end;

procedure InitializeWizard();
begin
  if FindPreviousInstall() then
    CreateOutputMsgPage(wpWelcome, CustomMessage('UpdateTitle'), CustomMessage('UpdateSubtitle'),
      FmtMessage(CustomMessage('UpdateInfo'), [PreviousVersion, PreviousDir, '{#AppVersion}']));
end;

function BinaryExists(Name: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\bin\') + Name);
end;

procedure RunBuild();
var
  ResultCode: Integer;
  Params, LogFile: String;
begin
  LogFile := ExpandConstant('{app}\install-log.txt');
  if PreviousDir <> '' then
    WizardForm.StatusLabel.Caption := CustomMessage('Updating')
  else
    WizardForm.StatusLabel.Caption := CustomMessage('Building');
  Params := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\tools\build-and-install.ps1') + '"' +
            ' -StagingDir "' + ExpandConstant('{app}\staging') + '"' +
            ' -SourceDir "' + ExpandConstant('{app}\src') + '"' +
            ' -InstallDir "' + ExpandConstant('{app}') + '"' +
            ' -LogFile "' + LogFile + '"' +
            ' -Language ' + ActiveLanguage();
  if not Exec('powershell.exe', Params, '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
    SuppressibleMsgBox(FmtMessage(CustomMessage('BuildFailed'), [LogFile]), mbError, MB_OK, IDOK);
end;
