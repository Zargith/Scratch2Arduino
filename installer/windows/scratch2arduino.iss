; Scratch2Arduino - Windows installer (Inno Setup 6).
; Builds installer.exe:  iscc installer\windows\scratch2arduino.iss
;
; The installer ships the sources, then runs build-and-install.ps1, which installs the missing
; prerequisites (CMake, Visual Studio Build Tools, Qt, WebView2) and builds Scratch2Arduino
; with CMake directly on the user's machine.

#define AppName "Scratch2Arduino"
#ifndef AppVersion
  #define AppVersion "0.1.0"
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
LicenseFile={#SourceRoot}\LICENSE
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
ShowLanguageDialog=auto

[Languages]
; The first language is the fallback: French by default.
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
french.TypeFull=Installation complète (les deux variantes)
english.TypeFull=Full installation (both variants)
french.TypeCustom=Installation personnalisée
english.TypeCustom=Custom installation
french.CompWebview=Variante A — webview (éditeur scratch-blocks)
english.CompWebview=Variant A — webview (scratch-blocks editor)
french.CompNative=Variante B — native (Qt)
english.CompNative=Variant B — native (Qt)
french.Building=Compilation de Scratch2Arduino avec CMake (cela peut prendre plusieurs minutes)...
english.Building=Building Scratch2Arduino with CMake (this can take several minutes)...
french.BuildFailed=La compilation de Scratch2Arduino a échoué.%n%nConsulte le journal :%n%1
english.BuildFailed=Building Scratch2Arduino failed.%n%nSee the log:%n%1
french.NameWebview=Scratch2Arduino (webview)
english.NameWebview=Scratch2Arduino (webview)
french.NameNative=Scratch2Arduino (native)
english.NameNative=Scratch2Arduino (native)

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "webview"; Description: "{cm:CompWebview}"; Types: full custom
Name: "native"; Description: "{cm:CompNative}"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceRoot}\*"; DestDir: "{app}\src"; Excludes: "\build,\.git,\.github,\installer\windows\Output,\frontend\node_modules"; Flags: recursesubdirs createallsubdirs ignoreversion
; Must stay last: once every file is copied, AfterInstall runs the CMake build.
Source: "build-and-install.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion; AfterInstall: RunBuild

[Icons]
Name: "{group}\{cm:NameWebview}"; Filename: "{app}\bin\scratch2arduino-webview.exe"; Components: webview; Check: BinaryExists('scratch2arduino-webview.exe')
Name: "{group}\{cm:NameNative}"; Filename: "{app}\bin\scratch2arduino-native.exe"; Components: native; Check: BinaryExists('scratch2arduino-native.exe')
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{cm:NameWebview}"; Filename: "{app}\bin\scratch2arduino-webview.exe"; Components: webview; Tasks: desktopicon; Check: BinaryExists('scratch2arduino-webview.exe')
Name: "{autodesktop}\{cm:NameNative}"; Filename: "{app}\bin\scratch2arduino-native.exe"; Components: native; Tasks: desktopicon; Check: BinaryExists('scratch2arduino-native.exe')

[UninstallDelete]
; Files produced by the CMake build are not tracked by Inno Setup.
Type: filesandordirs; Name: "{app}"

[Code]
function BinaryExists(Name: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\bin\') + Name);
end;

function VariantArgs(): String;
begin
  Result := '';
  if not WizardIsComponentSelected('webview') then
    Result := Result + ' -NoWebview';
  if not WizardIsComponentSelected('native') then
    Result := Result + ' -NoNative';
end;

procedure RunBuild();
var
  ResultCode: Integer;
  Params, LogFile: String;
begin
  LogFile := ExpandConstant('{app}\install-log.txt');
  WizardForm.StatusLabel.Caption := CustomMessage('Building');
  Params := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\tools\build-and-install.ps1') + '"' +
            ' -SourceDir "' + ExpandConstant('{app}\src') + '"' +
            ' -InstallDir "' + ExpandConstant('{app}') + '"' +
            ' -LogFile "' + LogFile + '"' +
            ' -Language ' + ActiveLanguage() + VariantArgs();
  if not Exec('powershell.exe', Params, '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
    SuppressibleMsgBox(FmtMessage(CustomMessage('BuildFailed'), [LogFile]), mbError, MB_OK, IDOK);
end;
