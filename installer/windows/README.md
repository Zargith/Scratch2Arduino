# Windows installer

`Scratch2Arduino_installer.exe` is built with [Inno Setup 6.3 or later](https://jrsoftware.org/isinfo.php).
The CI first installs a prebuilt Scratch2Arduino into a folder, then packs it:

```powershell
cmake -S . -B build-win -DS2A_BUILD_TESTS=OFF -DS2A_FRONTEND_DIST="$PWD\frontend\dist" -DS2A_BUNDLE_ARDUINO_CORES=arduino:avr
cmake --build build-win --config Release --parallel
cmake --install build-win --config Release --prefix installer\windows\prebuilt
iscc /DAppVersion=1.2.0 /DPrebuiltDir="$PWD\installer\windows\prebuilt" installer\windows\scratch2arduino.iss
```

Without `/DPrebuiltDir`, the installer can only build from the sources (below).

## What it does

1. asks for the language (French by default, English); when Scratch2Arduino is already installed, it says so
   and updates that installation in place;
2. if Scratch2Arduino is open, asks to save the project and close it (Retry / Cancel) - the same before
   uninstalling; the program declares the `Scratch2ArduinoRunning` mutex, older versions are found by their
   process name;
3. **prebuilt (default)**: copies the programs into `<install dir>\bin` - `Scratch2Arduino.exe`, `s2a-cli.exe`,
   the Visual C++ runtime DLLs, the block editor, `arduino-cli.exe` and `arduino-seed` (the Arduino AVR core:
   Uno, Nano, Mega). Nothing is compiled, and nothing is downloaded, except the WebView2 runtime on a Windows
   that does not have it (Microsoft's bootstrapper). The sources and build tree of an installation built
   before from the sources are removed;
4. **"Build Scratch2Arduino on this computer" (expert option, unchecked)**: unpacks the sources into
   `<install dir>\staging` and runs [`build-and-install.ps1`](build-and-install.ps1) without a console
   window. Its output is shown in the installer window, under the progress bar (the current step above it);
   the log can be scrolled back while it runs, and **Cancel** stops the build and the programs it started.
   The script
   - installs only the missing prerequisites with `winget` (CMake, the Visual Studio 2022 Build Tools C++
     compiler and Windows SDK, the WebView2 runtime) - those already present are kept;
   - copies into `<install dir>\src` only the source files whose content changed;
   - runs CMake configure, build and install, reusing the build tree kept in `<install dir>\build`, so an
     update recompiles only what changed (if that tree cannot be reused, it rebuilds everything);
   - removes the files of the previous version that are no longer installed;
5. creates the Start-menu (and optional desktop) shortcuts, opens `.s2a` projects with Scratch2Arduino
   (double-click, or a file dropped on the icon), and an uninstaller.

Silent installation (a classroom deployment, for example): `Scratch2Arduino_installer.exe /VERYSILENT /SUPPRESSMSGBOXES`;
add `/MERGETASKS=buildfromsource` to build from the sources. The choice is remembered: an installation built
from the sources is built again on update (uncheck the option, or `/MERGETASKS=!buildfromsource`, to go back
to the prebuilt one). Installations made before 1.2.0 become prebuilt.

## The shipped Arduino AVR core

With `-DS2A_BUNDLE_ARDUINO_CORES=arduino:avr`, the build runs the bundled arduino-cli once to download the core
and its tools, and installs their archives and the board indexes in `arduino-seed`, next to `arduino-cli.exe`
([`cmake/S2AArduinoSeed.cmake`](../../cmake/S2AArduinoSeed.cmake)). The first time a board needs the core,
Scratch2Arduino copies them into the user's private arduino-cli folders and installs that exact version without
the internet (`ArduinoCli::installPlatform`, `core/src/arduino_cli.cpp`). Other boards (ESP32, ESP8266) are
still downloaded when first used.

## Building from the sources without the installer

The build log is written to `<install dir>\install-log.txt`. The script also works from a source checkout:

```powershell
powershell -ExecutionPolicy Bypass -File installer\windows\build-and-install.ps1 -InstallDir "$env:LOCALAPPDATA\Scratch2Arduino"
```

Options: `-NoDeps` (never install prerequisites), `-CleanBuild` (full rebuild), `-Language english`,
`-StagingDir <folder>` (sync these sources into `-SourceDir` first). The installer also passes `-PidFile`,
`-DisplayFile` and `-ExitFile`: it runs the script without waiting for it, so that its window keeps responding
(Inno Setup's `Exec` and `ExecAndLogOutput` stop handling the window until the program ends), watches the
process, shows the lines of the display file and stops the process tree on Cancel. The CI runs the build
from the sources with the window shown and fails if it stops responding.

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
