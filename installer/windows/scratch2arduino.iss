; Scratch2Arduino - Windows installer (Inno Setup 6).
; Builds Scratch2Arduino_installer.exe:  iscc installer\windows\scratch2arduino.iss  (Inno Setup 6.3 or later)
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
  #define AppVersion "0.10.0"
#endif
#define SourceRoot "..\.."

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 or later is needed (ExecAndLogOutput shows the build in the installer window).
#endif

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
OutputBaseFilename=Scratch2Arduino_installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#SourceRoot}\resources\icons\scratch2arduino.ico
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\bin\scratch2arduino.exe
ShowLanguageDialog=auto
; Created by scratch2arduino.exe (0.6.4 and later) while it runs: setup asks to close it first.
; Older versions are found by their process name (IsAppRunning below).
AppMutex=Scratch2ArduinoRunning
; .s2a projects open in Scratch2Arduino when double-clicked.
ChangesAssociations=yes

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
french.AppRunning=Scratch2Arduino est ouvert.%n%nEnregistre ton projet, ferme Scratch2Arduino, puis clique sur "Réessayer\".
english.AppRunning=Scratch2Arduino is open.%n%nSave your project, close Scratch2Arduino, then click "Retry".
french.AppRunningCancelled=Installation annulée : Scratch2Arduino est encore ouvert.
english.AppRunningCancelled=Setup cancelled: Scratch2Arduino is still open.
french.BuildDetails=Détails de l'installation :
english.BuildDetails=Installation details:
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

[Registry]
Root: HKA; Subkey: "Software\Classes\.s2a\OpenWithProgids"; ValueType: string; ValueName: "Scratch2Arduino.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Scratch2Arduino.Project"; ValueType: string; ValueName: ""; ValueData: "Scratch2Arduino project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Scratch2Arduino.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\scratch2arduino.exe,0"
Root: HKA; Subkey: "Software\Classes\Scratch2Arduino.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\scratch2arduino.exe"" ""%1"""

[UninstallDelete]
; Files produced by the CMake build are not tracked by Inno Setup.
Type: filesandordirs; Name: "{app}"

[Code]
var
  PreviousVersion, PreviousDir: String;
  BuildOutput: TNewMemo;

// True while scratch2arduino.exe runs (any version, even those without AppMutex).
function IsAppRunning(): Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''scratch2arduino.exe''');
    Result := Processes.Count > 0;
  except
    // WMI unavailable: the build reports a locked file if the program is still open.
  end;
end;

// Asks to close the program until it is closed (Retry) or the user gives up (Cancel).
function WaitUntilAppClosed(): Boolean;
begin
  Result := True;
  while IsAppRunning() do
    if SuppressibleMsgBox(CustomMessage('AppRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not WaitUntilAppClosed() then
    Result := CustomMessage('AppRunningCancelled');
end;

function InitializeUninstall(): Boolean;
begin
  Result := WaitUntilAppClosed();
end;

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
var
  Caption: TNewStaticText;
begin
  // The build runs hidden: its output is shown here, below the progress bar.
  Caption := TNewStaticText.Create(WizardForm);
  Caption.Parent := WizardForm.InstallingPage;
  Caption.Caption := CustomMessage('BuildDetails');
  Caption.Top := WizardForm.ProgressGauge.Top + WizardForm.ProgressGauge.Height + ScaleY(12);
  Caption.Left := 0;
  BuildOutput := TNewMemo.Create(WizardForm);
  BuildOutput.Parent := WizardForm.InstallingPage;
  BuildOutput.Left := 0;
  BuildOutput.Top := Caption.Top + Caption.Height + ScaleY(4);
  BuildOutput.Width := WizardForm.InstallingPage.ClientWidth;
  BuildOutput.Height := WizardForm.InstallingPage.ClientHeight - BuildOutput.Top;
  BuildOutput.ReadOnly := True;
  BuildOutput.ScrollBars := ssVertical;
  BuildOutput.WordWrap := False;
  BuildOutput.Font.Name := 'Consolas';
  BuildOutput.Font.Size := 8;
  if FindPreviousInstall() then
    CreateOutputMsgPage(wpWelcome, CustomMessage('UpdateTitle'), CustomMessage('UpdateSubtitle'),
      FmtMessage(CustomMessage('UpdateInfo'), [PreviousVersion, PreviousDir, '{#AppVersion}']));
end;

function BinaryExists(Name: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\bin\') + Name);
end;

// Each line written by build-and-install.ps1, while it runs.
procedure ShowBuildLine(const S: String; const Error, FirstLine: Boolean);
begin
  BuildOutput.Lines.Add(S);
  BuildOutput.SelStart := Length(BuildOutput.Text); // keep the last line in view
  if Copy(S, 1, 4) = '==> ' then
    WizardForm.FilenameLabel.Caption := Copy(S, 5, Length(S) - 4); // the current step
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
  WizardForm.ProgressGauge.Style := npbstMarquee; // the length of the build is not known
  try
    // No console window: the output goes to the memo of the installing page.
    if not ExecAndLogOutput('powershell.exe', Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode, @ShowBuildLine) or
       (ResultCode <> 0) then
      SuppressibleMsgBox(FmtMessage(CustomMessage('BuildFailed'), [LogFile]), mbError, MB_OK, IDOK);
  finally
    WizardForm.ProgressGauge.Style := npbstNormal;
  end;
end;
