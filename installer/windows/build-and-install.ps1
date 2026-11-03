<#
.SYNOPSIS
    Installs the prerequisites, then builds and installs Scratch2Arduino with CMake (Windows).

.DESCRIPTION
    Run by installer.exe after the sources are copied, but it can also be used on its own:
        powershell -ExecutionPolicy Bypass -File installer\windows\build-and-install.ps1
    Missing tools are installed with winget: CMake, Visual Studio 2022 Build Tools (C++),
    the WebView2 runtime (variant A) and Qt 6 through aqtinstall (variant B).
#>
[CmdletBinding()]
param(
    [string]$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$InstallDir = (Join-Path $env:ProgramFiles 'Scratch2Arduino'),
    [string]$BuildDir = '',
    [string]$LogFile = '',
    [switch]$NoWebview,
    [switch]$NoNative,
    [switch]$NoDeps,
    [switch]$KeepBuild,
    [ValidateSet('french', 'english', 'fr', 'en')]
    [string]$Language = 'french',
    [string]$QtVersion = '6.8.3',
    [string]$QtRoot = 'C:\Qt'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$French = $Language -in @('french', 'fr')
if (-not $BuildDir) { $BuildDir = Join-Path $InstallDir 'build' }
if (-not $LogFile) { $LogFile = Join-Path $InstallDir 'install-log.txt' }
New-Item -ItemType Directory -Force -Path (Split-Path $LogFile) | Out-Null

function Say([string]$Fr, [string]$En) {
    $text = if ($French) { $Fr } else { $En }
    Write-Host "==> $text" -ForegroundColor Cyan
    Add-Content -Path $LogFile -Value "==> $text" -Encoding UTF8
}

function Fail([string]$Fr, [string]$En) {
    $text = if ($French) { $Fr } else { $En }
    Write-Host $text -ForegroundColor Red
    Add-Content -Path $LogFile -Value "ERROR: $text" -Encoding UTF8
    exit 1
}

# Runs a native command, mirrors its output to the log and stops on a non-zero exit code.
function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    Add-Content -Path $LogFile -Value "> $Exe $($Arguments -join ' ')" -Encoding UTF8
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    # Not Tee-Object: in Windows PowerShell 5.1 it writes UTF-16, which would mix encodings in the log.
    & $Exe @Arguments 2>&1 | ForEach-Object {
        $line = "$_"
        Write-Host $line
        Add-Content -Path $LogFile -Value $line -Encoding UTF8
    }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($code -ne 0) { throw "$Exe exited with code $code" }
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Install-WithWinget([string]$Id, [string[]]$Extra = @()) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Fail "winget est introuvable : installe « $Id » manuellement puis relance l'installation." `
             "winget not found: install '$Id' manually, then run the installer again."
    }
    Say "Installation de $Id..." "Installing $Id..."
    $arguments = @('install', '--id', $Id, '--exact', '--silent', '--accept-package-agreements',
                   '--accept-source-agreements') + $Extra
    Invoke-Native 'winget' $arguments
    Update-SessionPath
}

function Find-CMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidate = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
    if (Test-Path $candidate) { return $candidate }
    return $null
}

function Test-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $false }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    return [bool]$path
}

function Test-WebView2 {
    $keys = @(
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
        'HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
        'HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
    )
    foreach ($key in $keys) {
        $version = (Get-ItemProperty -Path $key -Name pv -ErrorAction SilentlyContinue).pv
        if ($version -and $version -ne '0.0.0.0') { return $true }
    }
    return $false
}

function Find-Qt {
    foreach ($candidate in @($env:QT_ROOT_DIR, $env:Qt6_DIR)) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    if (Test-Path $QtRoot) {
        $found = Get-ChildItem -Path $QtRoot -Directory -Filter '6.*' -ErrorAction SilentlyContinue |
            Sort-Object { [version]($_.Name -replace '[^0-9.]', '') } -Descending |
            ForEach-Object { Get-ChildItem -Path $_.FullName -Directory -Filter 'msvc20*_64' } |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

function Install-Qt {
    $python = Get-Command py -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }
    if (-not $python) {
        Install-WithWinget 'Python.Python.3.12'
        $python = Get-Command py -ErrorAction SilentlyContinue
        if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }
        if (-not $python) { Fail 'Python est introuvable après son installation.' 'Python not found after installing it.' }
    }
    Say "Installation de Qt $QtVersion (cela peut être long)..." "Installing Qt $QtVersion (this can take a while)..."
    Invoke-Native $python.Source @('-m', 'pip', 'install', '--upgrade', '--quiet', 'aqtinstall')
    Invoke-Native $python.Source @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $QtVersion,
                                   'win64_msvc2022_64', '--outputdir', $QtRoot)
}

try {
    Set-Content -Path $LogFile -Value "Scratch2Arduino build - $(Get-Date -Format s)" -Encoding UTF8
    $buildWebview = -not $NoWebview
    $buildNative = -not $NoNative
    if (-not $buildWebview -and -not $buildNative) {
        Fail 'Aucune variante sélectionnée.' 'No variant selected.'
    }

    # --- Prerequisites ----------------------------------------------------------
    Say 'Vérification des prérequis' 'Checking prerequisites'
    $cmake = Find-CMake
    if (-not $cmake) {
        if ($NoDeps) { Fail 'CMake est introuvable.' 'CMake not found.' }
        Install-WithWinget 'Kitware.CMake'
        $cmake = Find-CMake
        if (-not $cmake) { Fail 'CMake est introuvable après son installation.' 'CMake not found after installing it.' }
    }

    if (-not (Test-VisualStudio)) {
        if ($NoDeps) { Fail 'Le compilateur C++ de Visual Studio est introuvable.' 'Visual Studio C++ compiler not found.' }
        Install-WithWinget 'Microsoft.VisualStudio.2022.BuildTools' @('--override',
            '--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
        if (-not (Test-VisualStudio)) { Fail "L'installation de Visual Studio Build Tools a échoué." 'Visual Studio Build Tools installation failed.' }
    }

    if ($buildWebview -and -not (Test-WebView2) -and -not $NoDeps) {
        Install-WithWinget 'Microsoft.EdgeWebView2Runtime'
    }

    $qtDir = $null
    if ($buildNative) {
        $qtDir = Find-Qt
        if (-not $qtDir) {
            if ($NoDeps) { Fail 'Qt 6 est introuvable.' 'Qt 6 not found.' }
            Install-Qt
            $qtDir = Find-Qt
            if (-not $qtDir) { Fail "Qt 6 est introuvable après son installation." 'Qt 6 not found after installing it.' }
        }
        Say "Qt trouvé : $qtDir" "Found Qt: $qtDir"
    }

    # --- Configure, build, install ----------------------------------------------
    $onOff = { param($value) if ($value) { 'ON' } else { 'OFF' } }
    $configure = @('-S', $SourceDir, '-B', $BuildDir,
                   "-DCMAKE_INSTALL_PREFIX=$InstallDir",
                   "-DS2A_BUILD_WEBVIEW=$(& $onOff $buildWebview)",
                   "-DS2A_BUILD_NATIVE=$(& $onOff $buildNative)",
                   '-DS2A_BUILD_TESTS=OFF')
    if ($qtDir) { $configure += "-DCMAKE_PREFIX_PATH=$qtDir" }

    Say 'Configuration (CMake)' 'Configuring (CMake)'
    Invoke-Native $cmake $configure
    Say 'Compilation' 'Building'
    Invoke-Native $cmake @('--build', $BuildDir, '--config', 'Release', '--parallel')
    Say "Installation dans $InstallDir" "Installing into $InstallDir"
    Invoke-Native $cmake @('--install', $BuildDir, '--config', 'Release')

    if (-not $KeepBuild) {
        Remove-Item -Recurse -Force -Path $BuildDir -ErrorAction SilentlyContinue
    }
    Say 'Scratch2Arduino est installé !' 'Scratch2Arduino is installed!'
    exit 0
}
catch {
    Add-Content -Path $LogFile -Value "ERROR: $_" -Encoding UTF8
    Write-Host $_ -ForegroundColor Red
    Fail "La compilation a échoué. Journal : $LogFile" "The build failed. Log: $LogFile"
}
