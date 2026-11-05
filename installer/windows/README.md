# Windows installer

`Scratch2Arduino_installer.exe` is built with [Inno Setup 6.3 or later](https://jrsoftware.org/isinfo.php):

```powershell
iscc installer\windows\scratch2arduino.iss          # -> installer\windows\Output\Scratch2Arduino_installer.exe
iscc /DAppVersion=0.2.0 installer\windows\scratch2arduino.iss
```

When run, the installer:

1. asks for the language (French by default, English); when Scratch2Arduino is already installed, it says so
   and updates that installation in place;
2. if Scratch2Arduino is open, asks to save the project and close it (Retry / Cancel) — the same before
   uninstalling; the program declares the `Scratch2ArduinoRunning` mutex, older versions are found by their
   process name;
3. unpacks the sources into `<install dir>\staging`;
4. runs [`build-and-install.ps1`](build-and-install.ps1) without a console window: its output is shown in the
   installer window, under the progress bar (the current step above it). The script
   - installs only the missing prerequisites with `winget` (CMake, Visual Studio 2022 Build Tools with the
     C++ workload, the WebView2 runtime) — those already present are kept;
   - copies into `<install dir>\src` only the source files whose content changed;
   - runs CMake configure, build and install, reusing the build tree kept in `<install dir>\build`, so an
     update recompiles only what changed (if that tree cannot be reused, it rebuilds everything);
   - removes the files of the previous version that are no longer installed;
5. creates the Start-menu (and optional desktop) shortcuts, opens `.s2a` projects with Scratch2Arduino
   (double-click, or a file dropped on the icon), and an uninstaller.

The build log is written to `<install dir>\install-log.txt`.

The script also works without the installer, from a source checkout:

```powershell
powershell -ExecutionPolicy Bypass -File installer\windows\build-and-install.ps1 -InstallDir "$env:LOCALAPPDATA\Scratch2Arduino"
```

Options: `-NoDeps` (never install prerequisites), `-CleanBuild` (full rebuild), `-Language english`,
`-StagingDir <folder>` (sync these sources into `-SourceDir` first).

## Windows SmartScreen and antivirus warnings

An unsigned installer downloaded from the internet is shown by Windows as coming from an *unknown
publisher* ("Windows protected your PC"), and some antivirus programs are wary of an installer that runs
PowerShell and installs other programs. The cure is to **sign** it with a code signing certificate:

- **Azure Trusted Signing** (Microsoft, a few euros per month) or an **OV** certificate from a certificate
  authority: SmartScreen trusts the publisher once enough people have installed signed versions;
- an **EV** certificate (more expensive, hardware token or cloud HSM): trusted at once.

The CI signs `Scratch2Arduino_installer.exe` when the repository has the secrets `WINDOWS_CERTIFICATE`
(the `.pfx` file in base64: `[Convert]::ToBase64String([IO.File]::ReadAllBytes("cert.pfx"))`) and
`WINDOWS_CERTIFICATE_PASSWORD`. Without them the installer stays unsigned. A false positive can also be
reported to Microsoft at <https://www.microsoft.com/wdsi/filesubmission>.
