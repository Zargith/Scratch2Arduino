# Windows installer

`installer.exe` is built with [Inno Setup 6](https://jrsoftware.org/isinfo.php):

```powershell
iscc installer\windows\scratch2arduino.iss          # -> installer\windows\Output\installer.exe
iscc /DAppVersion=0.2.0 installer\windows\scratch2arduino.iss
```

When run, the installer:

1. asks for the language (French by default, English); when Scratch2Arduino is already installed, it says so
   and updates that installation in place;
2. unpacks the sources into `<install dir>\staging`;
3. runs [`build-and-install.ps1`](build-and-install.ps1), which
   - installs only the missing prerequisites with `winget` (CMake, Visual Studio 2022 Build Tools with the
     C++ workload, the WebView2 runtime) — those already present are kept;
   - copies into `<install dir>\src` only the source files whose content changed;
   - runs CMake configure, build and install, reusing the build tree kept in `<install dir>\build`, so an
     update recompiles only what changed (if that tree cannot be reused, it rebuilds everything);
   - removes the files of the previous version that are no longer installed;
4. creates the Start-menu (and optional desktop) shortcuts and an uninstaller.

The build log is written to `<install dir>\install-log.txt`.

The script also works without the installer, from a source checkout:

```powershell
powershell -ExecutionPolicy Bypass -File installer\windows\build-and-install.ps1 -InstallDir "$env:LOCALAPPDATA\Scratch2Arduino"
```

Options: `-NoDeps` (never install prerequisites), `-CleanBuild` (full rebuild), `-Language english`,
`-StagingDir <folder>` (sync these sources into `-SourceDir` first).
