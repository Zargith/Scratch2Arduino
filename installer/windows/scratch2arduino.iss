; Scratch2Arduino - Windows installer (Inno Setup 6).
; Builds Scratch2Arduino_installer.exe:  iscc installer\windows\scratch2arduino.iss  (Inno Setup 6.3 or later)
;
; Two ways to install:
;  - prebuilt (default, when compiled with /DPrebuiltDir=<folder>): the files of
;    `cmake --install` made by the CI (programs, Visual C++ runtime, block editor, arduino-cli and
;    the shipped Arduino AVR core) are copied into {app}. Nothing to download or compile, except
;    the WebView2 runtime when Windows does not have it;
;  - from the sources (task "buildfromsource", the only way without PrebuiltDir): the sources are
;    unpacked, then build-and-install.ps1 installs the missing prerequisites (CMake, Visual Studio
;    Build Tools, WebView2) and builds Scratch2Arduino with CMake on the user's machine. Updates
;    are incremental: only the changed files in {app}\src are recompiled in {app}\build.

#define AppName "Scratch2Arduino"
#ifndef AppVersion
  #define AppVersion "0.10.0"
#endif
#define SourceRoot "..\.."

#ifdef PrebuiltDir
  #if !DirExists(PrebuiltDir)
    #error PrebuiltDir does not exist (cmake --install <build> --prefix <PrebuiltDir>)
  #endif
#endif

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
french.UpdateInfo=Scratch2Arduino %1 est déjà installé dans :%n%2%n%nIl sera mis à jour vers la version %3.
english.UpdateInfo=Scratch2Arduino %1 is already installed in:%n%2%n%nIt will be updated to version %3.
french.BuildFromSource=Compiler Scratch2Arduino sur cet ordinateur (experts : installe Visual Studio et CMake, long)
english.BuildFromSource=Build Scratch2Arduino on this computer (experts: installs Visual Studio and CMake, slow)
french.CancelBuildConfirm=Arrêter l'installation de Scratch2Arduino ?%n%nCe qui est déjà téléchargé ou compilé est gardé pour la prochaine fois.
english.CancelBuildConfirm=Stop installing Scratch2Arduino?%n%nWhat is already downloaded or built is kept for next time.
french.BuildCancelled=Installation annulée.
english.BuildCancelled=Setup cancelled.
french.InstallingWebView2=Installation de Microsoft WebView2 (affichage de l'éditeur)...
english.InstallingWebView2=Installing Microsoft WebView2 (shows the editor)...
french.WebView2Failed=Microsoft WebView2 n'a pas pu être installé : Scratch2Arduino en a besoin pour s'afficher.%n%nInstalle-le depuis https://developer.microsoft.com/microsoft-edge/webview2/ puis lance Scratch2Arduino.
english.WebView2Failed=Microsoft WebView2 could not be installed: Scratch2Arduino needs it to show its window.%n%nInstall it from https://developer.microsoft.com/microsoft-edge/webview2/ then start Scratch2Arduino.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
#ifdef PrebuiltDir
Name: "buildfromsource"; Description: "{cm:BuildFromSource}"; Flags: unchecked
#endif

[InstallDelete]
; Shortcuts of versions before 0.4 (two variants).
Type: files; Name: "{group}\Scratch2Arduino (webview).lnk"
Type: files; Name: "{group}\Scratch2Arduino (native).lnk"
Type: files; Name: "{autodesktop}\Scratch2Arduino (webview).lnk"
Type: files; Name: "{autodesktop}\Scratch2Arduino (native).lnk"
; Leftover of an interrupted update.
Type: filesandordirs; Name: "{app}\staging"
; Prebuilt over an installation built from the sources: its sources and build tree (several GB)
; are no longer used. The block editor and the shipped cores are replaced as a whole, so files
; of the previous version do not stay behind.
Type: filesandordirs; Name: "{app}\src"; Check: not BuildFromSource
Type: filesandordirs; Name: "{app}\build"; Check: not BuildFromSource
Type: filesandordirs; Name: "{app}\tools"; Check: not BuildFromSource
Type: files; Name: "{app}\install-log.txt"; Check: not BuildFromSource
Type: filesandordirs; Name: "{app}\bin\frontend"; Check: not BuildFromSource
Type: filesandordirs; Name: "{app}\bin\arduino-seed"; Check: not BuildFromSource

[Files]
#ifdef PrebuiltDir
Source: "{#PrebuiltDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; Check: not BuildFromSource
#endif
Source: "{#SourceRoot}\*"; DestDir: "{app}\staging"; Excludes: "\build,\build-*,\.git,\.github,\installer\windows\Output,\installer\windows\prebuilt,\frontend\node_modules"; Flags: recursesubdirs createallsubdirs ignoreversion; Check: BuildFromSource
; Must stay last: once every file is copied, AfterInstall runs the CMake build.
Source: "build-and-install.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion; Check: BuildFromSource; AfterInstall: RunBuild

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
const
  EM_GETFIRSTVISIBLELINE = $00CE;
  EM_LINESCROLL = $00B6;
  WM_VSCROLL = $0115;
  SB_BOTTOM = 7;
  MaxBuildLines = 2000; // lines kept in the window; install-log.txt keeps them all

var
  PreviousVersion, PreviousDir: String;
  BuildOutput: TNewMemo;
  BuildCaption: TNewStaticText;
  FollowedLine: Integer;     // first visible line the last time the output followed the new lines
  BuildCancelled: Boolean;
  SavedCancelClick: TNotifyEvent;

// True when Scratch2Arduino is built on this computer rather than copied prebuilt.
function BuildFromSource(): Boolean;
begin
#ifdef PrebuiltDir
  Result := WizardIsTaskSelected('buildfromsource');
#else
  Result := True;
#endif
end;

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
begin
  // The build runs hidden: its output is shown here, below the progress bar.
  BuildCaption := TNewStaticText.Create(WizardForm);
  BuildCaption.Parent := WizardForm.InstallingPage;
  BuildCaption.Caption := CustomMessage('BuildDetails');
  BuildCaption.Top := WizardForm.ProgressGauge.Top + WizardForm.ProgressGauge.Height + ScaleY(12);
  BuildCaption.Left := 0;
  BuildOutput := TNewMemo.Create(WizardForm);
  BuildOutput.Parent := WizardForm.InstallingPage;
  BuildOutput.Left := 0;
  BuildOutput.Top := BuildCaption.Top + BuildCaption.Height + ScaleY(4);
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

// Each line written by build-and-install.ps1, while it runs. Never reads the whole text of the
// memo (that slowed the window down until it froze), and follows the new lines only while the
// user has not scrolled up to read the older ones.
procedure ShowBuildLine(const S: String; const Error, FirstLine: Boolean);
var
  First: Integer;
  Follow: Boolean;
begin
  First := SendMessage(BuildOutput.Handle, EM_GETFIRSTVISIBLELINE, 0, 0);
  Follow := First >= FollowedLine;
  if BuildOutput.Lines.Count >= MaxBuildLines then
  begin
    BuildOutput.Lines.Delete(0);
    if First > 0 then
      First := First - 1;
  end;
  BuildOutput.Lines.Add(S);
  if Follow then
  begin
    SendMessage(BuildOutput.Handle, WM_VSCROLL, SB_BOTTOM, 0);
    FollowedLine := SendMessage(BuildOutput.Handle, EM_GETFIRSTVISIBLELINE, 0, 0);
  end
  else
    // Adding a line moves the view to it: put the view back where the user left it.
    SendMessage(BuildOutput.Handle, EM_LINESCROLL, 0,
      First - SendMessage(BuildOutput.Handle, EM_GETFIRSTVISIBLELINE, 0, 0));
  if Copy(S, 1, 4) = '==> ' then
    WizardForm.FilenameLabel.Caption := Copy(S, 5, Length(S) - 4); // the current step
end;

function BuildPidFile(): String;
begin
  Result := ExpandConstant('{app}\build.pid');
end;

// Cancel while the build runs: Inno Setup only checks its own Cancel between two copied files,
// so the build (and the programs it started: winget, CMake, the compiler) is stopped here.
procedure BuildCancelClick(Sender: TObject);
var
  Pid: AnsiString;
  ResultCode: Integer;
begin
  if BuildCancelled then
    Exit;
  if MsgBox(CustomMessage('CancelBuildConfirm'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then
    Exit;
  BuildCancelled := True;
  WizardForm.CancelButton.Enabled := False;
  WizardForm.StatusLabel.Caption := CustomMessage('BuildCancelled');
  if LoadStringFromFile(BuildPidFile(), Pid) and (Trim(String(Pid)) <> '') then
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/PID ' + Trim(String(Pid)) + ' /T /F', '', SW_HIDE,
         ewNoWait, ResultCode);
end;

procedure RunBuild();
var
  ResultCode: Integer;
  Params, LogFile: String;
  Succeeded: Boolean;
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
            ' -PidFile "' + BuildPidFile() + '"' +
            ' -Language ' + ActiveLanguage();
  WizardForm.ProgressGauge.Style := npbstMarquee; // the length of the build is not known
  BuildCancelled := False;
  FollowedLine := 0;
  SavedCancelClick := WizardForm.CancelButton.OnClick;
  WizardForm.CancelButton.OnClick := @BuildCancelClick;
  WizardForm.CancelButton.Enabled := True;
  try
    // No console window: the output goes to the memo of the installing page.
    Succeeded := ExecAndLogOutput('powershell.exe', Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode, @ShowBuildLine) and
                 (ResultCode = 0);
  finally
    WizardForm.CancelButton.OnClick := SavedCancelClick;
    WizardForm.ProgressGauge.Style := npbstNormal;
    DeleteFile(BuildPidFile());
  end;
  if BuildCancelled then
    // Stops the installation: Inno Setup rolls back what it installed.
    RaiseException(CustomMessage('BuildCancelled'));
  if not Succeeded then
    SuppressibleMsgBox(FmtMessage(CustomMessage('BuildFailed'), [LogFile]), mbError, MB_OK, IDOK);
end;

// Same check as Test-WebView2 in build-and-install.ps1.
function WebView2Installed(): Boolean;
var
  Version: String;
begin
  Result :=
    (RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) and (Version <> '') and (Version <> '0.0.0.0')) or
    (RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) and (Version <> '') and (Version <> '0.0.0.0')) or
    (RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) and (Version <> '') and (Version <> '0.0.0.0'));
end;

// Prebuilt installation: WebView2 is part of Windows 11 and of an up-to-date Windows 10. When it
// is missing, Microsoft's small bootstrapper installs it.
procedure InstallWebView2();
var
  ResultCode: Integer;
begin
  if WebView2Installed() then
    Exit;
  WizardForm.StatusLabel.Caption := CustomMessage('InstallingWebView2');
  WizardForm.ProgressGauge.Style := npbstMarquee;
  try
    try
      DownloadTemporaryFile('https://go.microsoft.com/fwlink/p/?LinkId=2124703', 'MicrosoftEdgeWebview2Setup.exe', '', nil);
      if not Exec(ExpandConstant('{tmp}\MicrosoftEdgeWebview2Setup.exe'), '/silent /install', '', SW_HIDE,
                  ewWaitUntilTerminated, ResultCode) or not WebView2Installed() then
        SuppressibleMsgBox(CustomMessage('WebView2Failed'), mbError, MB_OK, IDOK);
    except
      SuppressibleMsgBox(CustomMessage('WebView2Failed'), mbError, MB_OK, IDOK);
    end;
  finally
    WizardForm.ProgressGauge.Style := npbstNormal;
  end;
end;

// The build output is only shown when Scratch2Arduino is built here.
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpInstalling then
  begin
    BuildCaption.Visible := BuildFromSource();
    BuildOutput.Visible := BuildFromSource();
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and not BuildFromSource() then
    InstallWebView2();
end;
