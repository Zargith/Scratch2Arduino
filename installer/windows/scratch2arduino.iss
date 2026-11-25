; Scratch2Arduino - Windows installer (Inno Setup 6).
; Builds Scratch2Arduino_installer.exe:  iscc installer\windows\scratch2arduino.iss  (Inno Setup 6.3 or later)
;
; The wizard first asks for its language (French or English), then for the kind of installation:
;  - simple: the usual folder, nothing else to choose;
;  - advanced: the folder can be chosen, and the expert option "buildfromsource" is offered.
; Then the tasks page: the optional boards (with their sizes), the expert option (advanced only),
; and the desktop icon, last.
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
;
; Optional boards (ESP32, ESP8266...): one task per package of resources/board-packages.json,
; written by board-packages.cmake (cmake -P installer\windows\board-packages.cmake) before ISCC.
; The chosen packages are downloaded at the end with s2a-cli, as the user who started the
; installer (they go in his %LOCALAPPDATA%). Packages already there show as installed, greyed.

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

#define HaveBoards FileExists(AddBackslash(SourcePath) + "board-code.isi")

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 or later is needed.
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
; Shown in advanced mode only, and never for an update (ShouldSkipPage).
DisableDirPage=no
LicenseFile={#SourceRoot}\LICENSE
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=Scratch2Arduino_installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Bigger than the default: the tasks page lists the boards with their sizes.
WizardSizePercent=120
SetupIconFile={#SourceRoot}\resources\icons\scratch2arduino.ico
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\bin\scratch2arduino.exe
; The language is the first thing asked, even when Windows' own language is French or English.
ShowLanguageDialog=yes
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
french.ModeTitle=Type d'installation
english.ModeTitle=Kind of installation
french.ModeSubtitle=Choisis comment installer Scratch2Arduino.
english.ModeSubtitle=Choose how to install Scratch2Arduino.
french.ModeCaption=En cas de doute, choisis l'installation simple.
english.ModeCaption=If unsure, choose the simple installation.
french.ModeSimple=Installation simple (conseillée) : Scratch2Arduino est installé dans le dossier habituel, prêt à l'emploi.
english.ModeSimple=Simple installation (advised): Scratch2Arduino is installed in the usual folder, ready to use.
french.ModeAdvanced=Installation avancée : choix du dossier d'installation, et option pour experts pour compiler Scratch2Arduino sur cet ordinateur.
english.ModeAdvanced=Advanced installation: choose the installation folder, and an option for experts to build Scratch2Arduino on this computer.
french.ExpertOptions=Option avancée :
english.ExpertOptions=Advanced option:
french.BoardPackages=Cartes supplémentaires (téléchargées à la fin de l'installation) :
english.BoardPackages=More boards (downloaded at the end of the installation):
french.BoardSizesHeader=téléchargement / disque
english.BoardSizesHeader=download / disk
french.BoardAlreadyInstalled=déjà installées
english.BoardAlreadyInstalled=already installed
french.BoardsNoneChosen=Les cartes Arduino AVR (Uno, Nano, Mega, Leonardo, Micro) sont toujours installées. Coche ci-dessous les autres cartes à ajouter : elles se suppriment ensuite dans les Paramètres de Scratch2Arduino.
english.BoardsNoneChosen=The Arduino AVR boards (Uno, Nano, Mega, Leonardo, Micro) are always installed. Tick below the other boards to add: remove them later in the Settings of Scratch2Arduino.
french.BoardsChosen=Cartes cochées : %1 à télécharger (connexion internet nécessaire), %2 sur le disque (%3 libres). Elles se suppriment ensuite dans les Paramètres de Scratch2Arduino.
english.BoardsChosen=Ticked boards: %1 to download (an Internet connection is needed), %2 on the disk (%3 free). Remove them later in the Settings of Scratch2Arduino.
french.BoardsNoRoom=Attention : il n'y a pas assez de place sur le disque pour les cartes cochées.
english.BoardsNoRoom=Warning: there is not enough room on the disk for the ticked boards.
french.UnitMB=Mo
english.UnitMB=MB
french.UnitGB=Go
english.UnitGB=GB
french.DecimalSeparator=,
english.DecimalSeparator=.
french.InstallingBoards=Téléchargement des cartes supplémentaires (cela peut prendre longtemps)...
english.InstallingBoards=Downloading the extra boards (this can take a long time)...
french.BoardsFailed=Les cartes supplémentaires n'ont pas pu être installées (pas de connexion internet ?).%n%nScratch2Arduino est bien installé : tu pourras les installer plus tard dans ses Paramètres.
english.BoardsFailed=The extra boards could not be installed (no Internet connection?).%n%nScratch2Arduino is installed: you can install them later in its Settings.
french.CancelBoardsConfirm=Arrêter le téléchargement des cartes supplémentaires ?%n%nScratch2Arduino reste installé : tu pourras les installer plus tard dans ses Paramètres.
english.CancelBoardsConfirm=Stop downloading the extra boards?%n%nScratch2Arduino stays installed: you can install them later in its Settings.
#if HaveBoards
  #include "board-messages.isi"
#endif
english.WebView2Failed=Microsoft WebView2 could not be installed: Scratch2Arduino needs it to show its window.%n%nInstall it from https://developer.microsoft.com/microsoft-edge/webview2/ then start Scratch2Arduino.

[Tasks]
; In this order on the page: the optional boards, the expert option (advanced mode only; the list
; is rebuilt each time the page is shown, so its Check follows the mode), the icons last.
#if HaveBoards
  #include "board-tasks.isi"
#endif
#ifdef PrebuiltDir
Name: "buildfromsource"; Description: "{cm:BuildFromSource}"; GroupDescription: "{cm:ExpertOptions}"; Flags: unchecked; Check: ExpertOptionShown
#endif
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

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

[Run]
; Last page: "Launch Scratch2Arduino" box, ticked (also after an update from the settings). Started
; as the user who ran the installer, not as the administrator.
Filename: "{app}\bin\scratch2arduino.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser; Check: BinaryExists('scratch2arduino.exe')

[Registry]
Root: HKA; Subkey: "Software\Classes\.s2a\OpenWithProgids"; ValueType: string; ValueName: "Scratch2Arduino.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Scratch2Arduino.Project"; ValueType: string; ValueName: ""; ValueData: "Scratch2Arduino project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Scratch2Arduino.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\scratch2arduino.exe,0"
Root: HKA; Subkey: "Software\Classes\Scratch2Arduino.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\scratch2arduino.exe"" ""%1"""

[UninstallDelete]
; Files produced by the CMake build are not tracked by Inno Setup: only the folders and files
; Scratch2Arduino creates are removed, never the whole folder (the advanced mode lets the user
; pick an existing folder such as D:\Tools, whose other files must stay, audit #171).
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\share"
Type: filesandordirs; Name: "{app}\lib"
Type: filesandordirs; Name: "{app}\libexec"
Type: filesandordirs; Name: "{app}\src"
Type: filesandordirs; Name: "{app}\build"
Type: filesandordirs; Name: "{app}\tools"
Type: filesandordirs; Name: "{app}\staging"
Type: files; Name: "{app}\install-log.txt"
Type: files; Name: "{app}\build-output.txt"
Type: files; Name: "{app}\build.exit"
Type: files; Name: "{app}\build.pid"
Type: dirifempty; Name: "{app}"

[Code]
const
  EM_GETFIRSTVISIBLELINE = $00CE;
  EM_LINESCROLL = $00B6;
  WM_VSCROLL = $0115;
  SB_BOTTOM = 7;
  MaxBuildLines = 2000; // lines kept in the window; install-log.txt keeps them all
  PM_REMOVE = 1;
  PROCESS_QUERY_LIMITED_INFORMATION = $1000;
  STILL_ACTIVE = 259;
  WatchStep = 50;          // ms between two looks at the watched process
  WatchStartTimeout = 120; // s for the watched process to write its PID

type
  // Room for a Windows MSG (28 bytes in a 32-bit Setup, 48 in a 64-bit one): only Windows reads it.
  TMsgBuffer = record
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12: Longint;
  end;

var
  PreviousVersion, PreviousDir: String;
  BuildOutput: TNewMemo;
  BuildCaption: TNewStaticText;
  FollowedLine: Integer;     // first visible line the last time the output followed the new lines
  BuildCancelled: Boolean;
  SavedCancelClick: TNotifyEvent;
  BoardInstalled: array of Boolean; // by package, found when the wizard starts
  BoardsCancelled: Boolean;
  ModePage: TInputOptionWizardPage;  // simple (0) or advanced (1) installation
  BoardSummary: TNewStaticText;      // above the tasks list: sizes of the ticked boards

function PeekMessage(var Msg: TMsgBuffer; Wnd: HWND; FilterMin, FilterMax, Remove: Cardinal): Integer;
  external 'PeekMessageW@user32.dll stdcall';
function TranslateMessage(var Msg: TMsgBuffer): Integer;
  external 'TranslateMessage@user32.dll stdcall';
function DispatchMessage(var Msg: TMsgBuffer): Longint;
  external 'DispatchMessageW@user32.dll stdcall';
function OpenProcess(Access: Cardinal; Inherit: Integer; ProcessId: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function GetExitCodeProcess(Process: THandle; var ExitCode: Cardinal): Integer;
  external 'GetExitCodeProcess@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Integer;
  external 'CloseHandle@kernel32.dll stdcall';

// True when Scratch2Arduino is built on this computer rather than copied prebuilt.
#if HaveBoards
  #include "board-code.isi"
#else
function BoardTaskCount(): Integer;
begin
  Result := 0;
end;

function BoardTaskName(I: Integer): String;
begin
  Result := '';
end;

function BoardPackageId(I: Integer): String;
begin
  Result := '';
end;

function BoardPackageDir(I: Integer): String;
begin
  Result := '';
end;

function BoardDownloadMB(I: Integer): Integer;
begin
  Result := 0;
end;

function BoardDiskMB(I: Integer): Integer;
begin
  Result := 0;
end;
#endif

function AdvancedMode(): Boolean;
begin
  Result := (ModePage <> nil) and (ModePage.SelectedValueIndex = 1);
end;

// Check of the "buildfromsource" task. Silent installations (CI: /MERGETASKS=buildfromsource)
// have no mode page: the task is always there for them.
function ExpertOptionShown(): Boolean;
begin
  Result := WizardSilent() or AdvancedMode();
end;

// A size as the application writes it (decimal units): "1,9 Go" / "1.9 GB", "125 Mo" / "125 MB".
function FormatMB(const MB: Int64): String;
var
  Tenths: Int64;
begin
  if MB < 1000 then
    Result := IntToStr(MB) + ' ' + CustomMessage('UnitMB')
  else
  begin
    Tenths := (MB + 50) div 100;
    Result := IntToStr(Tenths div 10);
    if (Tenths < 100) and (Tenths mod 10 <> 0) then
      Result := Result + CustomMessage('DecimalSeparator') + IntToStr(Tenths mod 10);
    Result := Result + ' ' + CustomMessage('UnitGB');
  end;
end;

// True when Dir holds a folder (arduino-cli leaves the empty <arch> folder after an uninstall).
function HasSubfolder(const Dir: String): Boolean;
var
  Found: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Dir) + '*', Found) then
  try
    repeat
      if (Found.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0) and (Found.Name <> '.') and (Found.Name <> '..') then
      begin
        Result := True;
        Exit;
      end;
    until not FindNext(Found);
  finally
    FindClose(Found);
  end;
end;

// Where Scratch2Arduino's arduino-cli keeps the boards (toolchain::Directories::defaults()).
function BoardPackagesFolder(): String;
begin
  Result := ExpandConstant('{localappdata}\Scratch2Arduino\arduino\data\packages\');
end;

procedure FindInstalledBoards();
var
  I: Integer;
begin
  SetArrayLength(BoardInstalled, BoardTaskCount());
  for I := 0 to BoardTaskCount() - 1 do
    BoardInstalled[I] := HasSubfolder(BoardPackagesFolder() + BoardPackageDir(I));
end;

// Index in the tasks list of the item with this caption, or -1.
function TaskItem(const Caption: String): Integer;
var
  I: Integer;
begin
  Result := -1;
  for I := 0 to WizardForm.TasksList.Items.Count - 1 do
    if WizardForm.TasksList.ItemCaption[I] = Caption then
    begin
      Result := I;
      Exit;
    end;
end;

// Above the tasks list: what the ticked boards (not installed yet) need.
procedure UpdateBoardSummary();
var
  I, Item: Integer;
  DownloadMB, DiskMB, FreeBytes, TotalBytes: Int64;
  Text: String;
begin
  if BoardSummary = nil then
    Exit;
  DownloadMB := 0;
  DiskMB := 0;
  for I := 0 to BoardTaskCount() - 1 do
    if not BoardInstalled[I] then
    begin
      Item := TaskItem(ExpandConstant('{cm:' + BoardTaskName(I) + '}'));
      if (Item >= 0) and WizardForm.TasksList.Checked[Item] then
      begin
        DownloadMB := DownloadMB + BoardDownloadMB(I);
        DiskMB := DiskMB + BoardDiskMB(I);
      end;
    end;
  if DiskMB = 0 then
    Text := CustomMessage('BoardsNoneChosen')
  else
  begin
    // The boards go into the user's folder (BoardPackagesFolder), usually on C:.
    if not GetSpaceOnDisk64(ExpandConstant('{localappdata}'), FreeBytes, TotalBytes) then
      FreeBytes := -1;
    if FreeBytes < 0 then
      Text := FmtMessage(CustomMessage('BoardsChosen'), [FormatMB(DownloadMB), FormatMB(DiskMB), '?'])
    else
    begin
      // One line: Inno Setup reads a line starting with "[" as a section, even in [Code].
      Text := FmtMessage(CustomMessage('BoardsChosen'), [FormatMB(DownloadMB), FormatMB(DiskMB), FormatMB(FreeBytes div 1000000)]);
      if FreeBytes div 1000000 < DiskMB + DownloadMB then
        Text := Text + #13#10 + CustomMessage('BoardsNoRoom');
    end;
  end;
  BoardSummary.Caption := Text;
end;

procedure TasksListClickCheck(Sender: TObject);
begin
  UpdateBoardSummary();
end;

// On the tasks page (its list is rebuilt each time it is shown): the sizes of each board package
// in a column on the right; a package already installed (an update, or installed from the
// application) is ticked, greyed and marked as installed.
procedure ShowBoardSizes();
var
  I, Item: Integer;
begin
  if BoardTaskCount() = 0 then
    Exit;
  Item := TaskItem(CustomMessage('BoardPackages'));
  if Item >= 0 then
    WizardForm.TasksList.ItemSubItem[Item] := CustomMessage('BoardSizesHeader');
  for I := 0 to BoardTaskCount() - 1 do
  begin
    Item := TaskItem(ExpandConstant('{cm:' + BoardTaskName(I) + '}'));
    if Item < 0 then
      continue;
    if BoardInstalled[I] then
    begin
      WizardForm.TasksList.ItemSubItem[Item] := CustomMessage('BoardAlreadyInstalled');
      WizardForm.TasksList.Checked[Item] := True;
      WizardForm.TasksList.ItemEnabled[Item] := False;
    end
    else
      WizardForm.TasksList.ItemSubItem[Item] :=
        FormatMB(BoardDownloadMB(I)) + ' / ' + FormatMB(BoardDiskMB(I));
  end;
  UpdateBoardSummary();
end;

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

// The kind of installation of the previous one: remembered since 1.3.0; before, an installation
// built from the sources was an advanced one.
function PreviousModeIndex(): Integer;
var
  Tasks: String;
begin
  Result := 0;
  if GetPreviousData('InstallMode', '') = 'advanced' then
    Result := 1
  else if (GetPreviousData('InstallMode', '') = '') and
          (RegQueryStringValue(HKLM, UninstallKey(), 'Inno Setup: Selected Tasks', Tasks) or
           RegQueryStringValue(HKCU, UninstallKey(), 'Inno Setup: Selected Tasks', Tasks)) and
          (Pos('buildfromsource', Lowercase(Tasks)) > 0) then
    Result := 1;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  // A silent installation built from the sources (no mode page) counts as an advanced one.
  if AdvancedMode() or BuildFromSource() then
    SetPreviousData(PreviousDataKey, 'InstallMode', 'advanced')
  else
    SetPreviousData(PreviousDataKey, 'InstallMode', 'simple');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // The folder: chosen in advanced mode only, and an update stays in the folder it has.
  Result := (PageID = wpSelectDir) and (not AdvancedMode() or (PreviousDir <> ''));
end;

// Room for BoardSummary between the text at the top of the tasks page and the list.
procedure CreateBoardSummary();
var
  I: Integer;
  Longest: String;
begin
  if BoardTaskCount() = 0 then
    Exit;
  BoardSummary := TNewStaticText.Create(WizardForm);
  BoardSummary.Parent := WizardForm.SelectTasksPage;
  BoardSummary.Left := WizardForm.TasksList.Left;
  BoardSummary.Top := WizardForm.TasksList.Top;
  BoardSummary.Width := WizardForm.TasksList.Width;
  BoardSummary.AutoSize := False;
  BoardSummary.WordWrap := True;
  // As high as its longest text: every board ticked, without enough room.
  Longest := FmtMessage(CustomMessage('BoardsChosen'), ['999,9 MB', '999,9 MB', '999,9 MB']) + #13#10 +
             CustomMessage('BoardsNoRoom');
  if Length(CustomMessage('BoardsNoneChosen')) > Length(Longest) then
    Longest := CustomMessage('BoardsNoneChosen');
  BoardSummary.Caption := Longest;
  BoardSummary.AdjustHeight();
  I := BoardSummary.Height + ScaleY(8);
  WizardForm.TasksList.Top := WizardForm.TasksList.Top + I;
  WizardForm.TasksList.Height := WizardForm.TasksList.Height - I;
  WizardForm.TasksList.OnClickCheck := @TasksListClickCheck;
end;

procedure InitializeWizard();
var
  UpdateAfter: Integer;
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
  FindInstalledBoards();
  CreateBoardSummary();
  // Right after the language: simple or advanced installation.
  ModePage := CreateInputOptionPage(wpWelcome, CustomMessage('ModeTitle'), CustomMessage('ModeSubtitle'),
    CustomMessage('ModeCaption'), True, False);
  ModePage.Add(CustomMessage('ModeSimple'));
  ModePage.Add(CustomMessage('ModeAdvanced'));
  UpdateAfter := ModePage.ID;
  if FindPreviousInstall() then
  begin
    ModePage.SelectedValueIndex := PreviousModeIndex();
    CreateOutputMsgPage(UpdateAfter, CustomMessage('UpdateTitle'), CustomMessage('UpdateSubtitle'),
      FmtMessage(CustomMessage('UpdateInfo'), [PreviousVersion, PreviousDir, '{#AppVersion}']));
  end
  else
    ModePage.SelectedValueIndex := 0;
end;

function BinaryExists(Name: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\bin\') + Name);
end;

// Lets the window work (move, scroll, Cancel) while a program runs: Inno Setup's own Exec does
// not, which froze the window during the whole build.
procedure ProcessWizardMessages();
var
  Msg: TMsgBuffer;
begin
  while PeekMessage(Msg, 0, 0, 0, PM_REMOVE) <> 0 do
  begin
    TranslateMessage(Msg);
    DispatchMessage(Msg);
  end;
end;

// Adds a line written by build-and-install.ps1 to the window. Never reads the whole text of the
// memo, and follows the new lines only while the user has not scrolled up to read older ones.
procedure ShowBuildLine(const S: String);
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

// Shows the lines of DisplayFile not shown yet. While the program runs, its last line may still
// be incomplete: it waits for the next look (or the end, All).
procedure ShowNewLines(const DisplayFile: String; var Shown: Integer; const All: Boolean);
var
  Lines: TArrayOfString;
  Count, I: Integer;
begin
  if (DisplayFile = '') or not LoadStringsFromLockedFile(DisplayFile, Lines) then
    Exit;
  Count := GetArrayLength(Lines);
  if not All then
    Count := Count - 1;
  for I := Shown to Count - 1 do
    ShowBuildLine(Lines[I]);
  if Count > Shown then
    Shown := Count;
end;

// Number written in a small text file (PID, exit code), or -1.
function ReadNumberFile(const FileName: String): Integer;
var
  Text: AnsiString;
begin
  Result := -1;
  if LoadStringFromLockedFile(FileName, Text) then
    Result := StrToIntDef(Trim(String(Text)), -1);
end;

// Runs a PowerShell command line without waiting for it, then waits for it while the window
// keeps working. The command writes its PID to PidFile at once, the lines to show to DisplayFile
// (optional) and its exit code to ExitFile before it ends. AsUser: runs it as the user who
// started the installer, not elevated. Returns False when it could not start.
function RunWatched(const Params, PidFile, DisplayFile, ExitFile: String; const AsUser: Boolean;
                    var ExitCode: Integer): Boolean;
var
  Process: THandle;
  Code: Cardinal;
  Pid, Shown, Waited: Integer;
  Finished: Boolean;
begin
  Result := False;
  ExitCode := -1;
  DeleteFile(PidFile);
  DeleteFile(ExitFile);
  if DisplayFile <> '' then
    DeleteFile(DisplayFile);
  if AsUser then
  begin
    if not ExecAsOriginalUser('powershell.exe', Params, '', SW_HIDE, ewNoWait, ExitCode) then
      Exit;
  end
  else if not Exec('powershell.exe', Params, '', SW_HIDE, ewNoWait, ExitCode) then
    Exit;
  Process := 0;
  Shown := 0;
  Waited := 0;
  Finished := False;
  repeat
    ProcessWizardMessages();
    Sleep(WatchStep);
    Waited := Waited + WatchStep;
    if Process = 0 then
    begin
      Pid := ReadNumberFile(PidFile);
      if Pid > 0 then
        Process := OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, Pid);
    end;
    if Process <> 0 then
    begin
      if (GetExitCodeProcess(Process, Code) <> 0) and (Code <> STILL_ACTIVE) then
      begin
        ExitCode := Code;
        Finished := True;
      end;
    end
    else if FileExists(ExitFile) then
    begin
      // Ended before it could be opened.
      ExitCode := ReadNumberFile(ExitFile);
      Finished := True;
    end
    else if Waited > WatchStartTimeout * 1000 then
      Exit; // never started
    if Waited mod 250 = 0 then
      ShowNewLines(DisplayFile, Shown, False);
  until Finished;
  if Process <> 0 then
    CloseHandle(Process);
  ShowNewLines(DisplayFile, Shown, True);
  DeleteFile(PidFile);
  DeleteFile(ExitFile);
  Result := True;
end;

function BuildPidFile(): String;
begin
  Result := ExpandConstant('{app}\build.pid');
end;

// Cancel while the build runs: Inno Setup only checks its own Cancel between two copied files,
// so the build (and the programs it started: winget, CMake, the compiler) is stopped here.
procedure BuildCancelClick(Sender: TObject);
var
  Pid, ResultCode: Integer;
begin
  if BuildCancelled then
    Exit;
  if MsgBox(CustomMessage('CancelBuildConfirm'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then
    Exit;
  BuildCancelled := True;
  WizardForm.CancelButton.Enabled := False;
  WizardForm.StatusLabel.Caption := CustomMessage('BuildCancelled');
  Pid := ReadNumberFile(BuildPidFile());
  if Pid > 0 then
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/PID ' + IntToStr(Pid) + ' /T /F', '', SW_HIDE,
         ewNoWait, ResultCode);
end;

procedure RunBuild();
var
  ExitCode: Integer;
  Params, LogFile, DisplayFile, ExitFile: String;
  Succeeded: Boolean;
begin
  LogFile := ExpandConstant('{app}\install-log.txt');
  DisplayFile := ExpandConstant('{app}\build-output.txt');
  ExitFile := ExpandConstant('{app}\build.exit');
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
            ' -DisplayFile "' + DisplayFile + '"' +
            ' -ExitFile "' + ExitFile + '"' +
            ' -Language ' + ActiveLanguage();
  WizardForm.ProgressGauge.Style := npbstMarquee; // the length of the build is not known
  BuildCancelled := False;
  FollowedLine := 0;
  SavedCancelClick := WizardForm.CancelButton.OnClick;
  WizardForm.CancelButton.OnClick := @BuildCancelClick;
  WizardForm.CancelButton.Enabled := True;
  try
    // No console window: the output goes to the memo of the installing page.
    Succeeded := RunWatched(Params, BuildPidFile(), DisplayFile, ExitFile, False, ExitCode) and (ExitCode = 0);
  finally
    WizardForm.CancelButton.OnClick := SavedCancelClick;
    WizardForm.ProgressGauge.Style := npbstNormal;
    DeleteFile(DisplayFile);
  end;
  if BuildCancelled then
    // Stops the installation: Inno Setup rolls back what it installed.
    RaiseException(CustomMessage('BuildCancelled'));
  if not Succeeded then
    // Stops the installation like a cancel: a message box only let setup register a version that
    // was never built, and silent installs exit with 0 (audit #205).
    RaiseException(FmtMessage(CustomMessage('BuildFailed'), [LogFile]));
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
  Params: String;
begin
  if WebView2Installed() then
    Exit;
  WizardForm.StatusLabel.Caption := CustomMessage('InstallingWebView2');
  WizardForm.ProgressGauge.Style := npbstMarquee;
  try
    try
      DownloadTemporaryFile('https://go.microsoft.com/fwlink/p/?LinkId=2124703', 'MicrosoftEdgeWebview2Setup.exe', '', nil);
      // Through PowerShell, so that the window keeps working while it runs (see RunWatched).
      Params := '-NoProfile -ExecutionPolicy Bypass -Command "' +
        'Set-Content -LiteralPath ''' + ExpandConstant('{tmp}\webview2.pid') + ''' -Value $PID; ' +
        '$p = Start-Process -FilePath ''' + ExpandConstant('{tmp}\MicrosoftEdgeWebview2Setup.exe') + ''' ' +
        '-ArgumentList ''/silent'',''/install'' -Wait -PassThru; ' +
        'Set-Content -LiteralPath ''' + ExpandConstant('{tmp}\webview2.exit') + ''' -Value $p.ExitCode; ' +
        'exit $p.ExitCode"';
      if not RunWatched(Params, ExpandConstant('{tmp}\webview2.pid'), '', ExpandConstant('{tmp}\webview2.exit'),
                        False, ResultCode) or not WebView2Installed() then
        SuppressibleMsgBox(CustomMessage('WebView2Failed'), mbError, MB_OK, IDOK);
    except
      SuppressibleMsgBox(CustomMessage('WebView2Failed'), mbError, MB_OK, IDOK);
    end;
  finally
    WizardForm.ProgressGauge.Style := npbstNormal;
  end;
end;

// Files of the board download: written by a process of the user (not elevated), read here.
function BoardsFile(const Name: String): String;
begin
  Result := ExpandConstant('{commonappdata}\Scratch2Arduino\setup\') + Name;
end;

// Cancel while the boards download: only the download stops, Scratch2Arduino stays installed.
procedure BoardsCancelClick(Sender: TObject);
var
  Pid, ResultCode: Integer;
begin
  if BoardsCancelled then
    Exit;
  if MsgBox(CustomMessage('CancelBoardsConfirm'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then
    Exit;
  BoardsCancelled := True;
  WizardForm.CancelButton.Enabled := False;
  Pid := ReadNumberFile(BoardsFile('boards.pid'));
  if Pid > 0 then
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/PID ' + IntToStr(Pid) + ' /T /F', '', SW_HIDE,
         ewNoWait, ResultCode);
end;

// The chosen board packages that are not installed yet, downloaded with s2a-cli board install.
procedure InstallBoards();
var
  I, ExitCode: Integer;
  Ids, Params: String;
  Succeeded: Boolean;
begin
  Ids := '';
  for I := 0 to BoardTaskCount() - 1 do
    if BoardInstalled[I] then
      Log('Board package already installed: ' + BoardPackageId(I))
    else if WizardIsTaskSelected(BoardTaskName(I)) then
      Ids := Ids + ' ' + BoardPackageId(I);
  if (Ids = '') or not BinaryExists('s2a-cli.exe') then
    Exit;
  Log('Board packages to install:' + Ids);
  ForceDirectories(ExpandConstant('{commonappdata}\Scratch2Arduino\setup'));
  WizardForm.StatusLabel.Caption := CustomMessage('InstallingBoards');
  WizardForm.FilenameLabel.Caption := '';
  BuildCaption.Visible := True;
  BuildOutput.Visible := True;
  WizardForm.ProgressGauge.Style := npbstMarquee;
  // UTF-8 both ways: the French messages of s2a-cli keep their accents.
  Params := '-NoProfile -ExecutionPolicy Bypass -Command "' +
    '[Console]::OutputEncoding = [Text.Encoding]::UTF8; ' +
    'Set-Content -LiteralPath ''' + BoardsFile('boards.pid') + ''' -Value $PID; ' +
    '& ''' + ExpandConstant('{app}\bin\s2a-cli.exe') + ''' board install' + Ids + ' --lang ' + Copy(ActiveLanguage(), 1, 2) + ' 2>&1 | ' +
    'ForEach-Object { Add-Content -LiteralPath ''' + BoardsFile('boards-output.txt') + ''' -Value ([string]$_) -Encoding UTF8 }; ' +
    '$code = $LASTEXITCODE; ' +
    'Set-Content -LiteralPath ''' + BoardsFile('boards.exit') + ''' -Value $code; exit $code"';
  BoardsCancelled := False;
  FollowedLine := 0;
  SavedCancelClick := WizardForm.CancelButton.OnClick;
  WizardForm.CancelButton.OnClick := @BoardsCancelClick;
  WizardForm.CancelButton.Enabled := True;
  try
    Succeeded := RunWatched(Params, BoardsFile('boards.pid'), BoardsFile('boards-output.txt'), BoardsFile('boards.exit'),
                            True, ExitCode) and (ExitCode = 0);
  finally
    WizardForm.CancelButton.OnClick := SavedCancelClick;
    WizardForm.ProgressGauge.Style := npbstNormal;
    DeleteFile(BoardsFile('boards-output.txt'));
  end;
  Log('Board packages: exit code ' + IntToStr(ExitCode));
  if not Succeeded and not BoardsCancelled then
    SuppressibleMsgBox(CustomMessage('BoardsFailed'), mbError, MB_OK, IDOK);
end;

// The build output is only shown when Scratch2Arduino is built here (and while boards download).
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectTasks then
    ShowBoardSizes();
  if CurPageID = wpInstalling then
  begin
    BuildCaption.Visible := BuildFromSource();
    BuildOutput.Visible := BuildFromSource();
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not BuildFromSource() then
      InstallWebView2();
    InstallBoards();
  end;
end;
