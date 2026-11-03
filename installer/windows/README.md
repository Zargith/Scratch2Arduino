# Windows installer

`installer.exe` is built with [Inno Setup 6](https://jrsoftware.org/isinfo.php):

```powershell
iscc installer\windows\scratch2arduino.iss          # -> installer\windows\Output\installer.exe
iscc /DAppVersion=0.2.0 installer\windows\scratch2arduino.iss
```

When run, the installer:

1. asks for the language (French by default, English) and the variants to install;
2. copies the sources into `<install dir>\src`;
3. runs [`build-and-install.ps1`](build-and-install.ps1), which installs what is missing with `winget`
   (CMake, Visual Studio 2022 Build Tools with the C++ workload, the WebView2 runtime, Python + `aqtinstall`
   for Qt 6) and then runs CMake configure, build and install into the installation directory;
4. creates the Start-menu (and optional desktop) shortcuts and an uninstaller.

The build log is written to `<install dir>\install-log.txt`.

The script also works without the installer, from a source checkout:

```powershell
powershell -ExecutionPolicy Bypass -File installer\windows\build-and-install.ps1 -InstallDir "$env:LOCALAPPDATA\Scratch2Arduino"
```

Options: `-NoWebview`, `-NoNative`, `-NoDeps`, `-KeepBuild`, `-Language english`, `-QtVersion 6.8.3`.
