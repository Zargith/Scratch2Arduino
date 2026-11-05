<#
.SYNOPSIS
    Installs the prerequisites, then builds and installs Scratch2Arduino with CMake (Windows).

.DESCRIPTION
    Run by installer.exe after the sources are copied, but it can also be used on its own:
        powershell -ExecutionPolicy Bypass -File installer\windows\build-and-install.ps1

    Updates are incremental:
      - prerequisites already on the computer (CMake, Visual Studio 2022 Build Tools, WebView2)
        are detected and not installed again; only the missing ones are installed with winget;
      - with -StagingDir (what installer.exe passes), the new sources are compared with the
        installed ones by content, and only the changed files are replaced, so the build tree
        kept in <InstallDir>\build recompiles only what changed;
      - files of the previous version that are no longer installed are removed.
    -CleanBuild forces a full rebuild.
#>
[CmdletBinding()]
param(
    [string]$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$StagingDir = '',
    [string]$InstallDir = (Join-Path $env:ProgramFiles 'Scratch2Arduino'),
    [string]$BuildDir = '',
    [string]$LogFile = '',
    [switch]$NoDeps,
    [switch]$CleanBuild,
    [ValidateSet('french', 'english', 'fr', 'en')]
    [string]$Language = 'french'
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

function Get-RelativeFiles([string]$Root) {
    $prefix = $Root.TrimEnd('\').Length + 1
    Get-ChildItem -Path $Root -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($prefix) }
}

function Test-SameContent([string]$A, [string]$B) {
    $itemA = Get-Item -LiteralPath $A -Force
    $itemB = Get-Item -LiteralPath $B -Force
    if ($itemA.Length -ne $itemB.Length) { return $false }
    return (Get-FileHash -LiteralPath $A -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $B -Algorithm SHA256).Hash
}

# Makes $Target identical to $Staging while touching only the files that differ, so that unchanged
# files keep their dates and the kept build tree does not recompile them. Returns the change count.
function Sync-Sources([string]$Staging, [string]$Target) {
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
    $now = Get-Date
    $changed = 0
    $wanted = @{}
    foreach ($relative in Get-RelativeFiles $Staging) {
        $wanted[$relative.ToLowerInvariant()] = $true
        $from = Join-Path $Staging $relative
        $to = Join-Path $Target $relative
        if ((Test-Path -LiteralPath $to) -and (Test-SameContent $from $to)) { continue }
        New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
        Copy-Item -LiteralPath $from -Destination $to -Force
        # A changed file must look newer than the objects built from its previous content.
        (Get-Item -LiteralPath $to -Force).LastWriteTime = $now
        $changed++
    }
    foreach ($relative in Get-RelativeFiles $Target) {
        if (-not $wanted.ContainsKey($relative.ToLowerInvariant())) {
            Remove-Item -LiteralPath (Join-Path $Target $relative) -Force
            $changed++
        }
    }
    Get-ChildItem -Path $Target -Recurse -Directory -Force | Sort-Object { $_.FullName.Length } -Descending |
        Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
    return $changed
}

function Read-Manifest([string]$Path) {
    if (Test-Path -LiteralPath $Path) { return @(Get-Content -LiteralPath $Path -Encoding UTF8) }
    return @()
}

try {
    Set-Content -Path $LogFile -Value "Scratch2Arduino build - $(Get-Date -Format s)" -Encoding UTF8

    if (Get-Process -Name 'scratch2arduino' -ErrorAction SilentlyContinue) {
        Fail 'Scratch2Arduino est ouvert : ferme-le puis relance l''installation.' `
             'Scratch2Arduino is running: close it, then run the installer again.'
    }

    # --- Prerequisites (only the missing ones are installed) --------------------
    Say 'Vérification des prérequis' 'Checking prerequisites'
    $cmake = Find-CMake
    if ($cmake) {
        Say "CMake déjà installé : $cmake" "CMake already installed: $cmake"
    } else {
        if ($NoDeps) { Fail 'CMake est introuvable.' 'CMake not found.' }
        Install-WithWinget 'Kitware.CMake'
        $cmake = Find-CMake
        if (-not $cmake) { Fail 'CMake est introuvable après son installation.' 'CMake not found after installing it.' }
    }

    if (Test-VisualStudio) {
        Say 'Compilateur C++ (Visual Studio) déjà installé' 'C++ compiler (Visual Studio) already installed'
    } else {
        if ($NoDeps) { Fail 'Le compilateur C++ de Visual Studio est introuvable.' 'Visual Studio C++ compiler not found.' }
        Install-WithWinget 'Microsoft.VisualStudio.2022.BuildTools' @('--override',
            '--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
        if (-not (Test-VisualStudio)) { Fail "L'installation de Visual Studio Build Tools a échoué." 'Visual Studio Build Tools installation failed.' }
    }

    if (Test-WebView2) {
        Say 'WebView2 déjà installé' 'WebView2 already installed'
    } elseif (-not $NoDeps) {
        Install-WithWinget 'Microsoft.EdgeWebView2Runtime'
    }

    # --- Sources: replace only what changed --------------------------------------
    if ($StagingDir) {
        Say 'Mise à jour des sources' 'Updating the sources'
        $changed = Sync-Sources $StagingDir $SourceDir
        Remove-Item -Recurse -Force -LiteralPath $StagingDir -ErrorAction SilentlyContinue
        Say "$changed fichier(s) modifié(s)" "$changed file(s) changed"
    }

    # --- Configure, build, install ----------------------------------------------
    if ($CleanBuild -and (Test-Path $BuildDir)) {
        Say 'Recompilation complète demandée' 'Full rebuild requested'
        Remove-Item -Recurse -Force -LiteralPath $BuildDir
    }
    $incremental = Test-Path (Join-Path $BuildDir 'CMakeCache.txt')
    if ($incremental) {
        Say 'Compilation précédente trouvée : seules les modifications seront recompilées' `
            'Previous build found: only the changes will be recompiled'
    }

    $configure = @('-S', $SourceDir, '-B', $BuildDir,
                   "-DCMAKE_INSTALL_PREFIX=$InstallDir",
                   '-DS2A_BUILD_WEBVIEW=ON',
                   '-DS2A_BUILD_TESTS=OFF')
    # installer.exe ships the block editor prebuilt: never rebuild it, even when Node.js is installed.
    $prebuiltFrontend = Join-Path $SourceDir 'frontend\dist'
    if (Test-Path (Join-Path $prebuiltFrontend 'index.html')) { $configure += "-DS2A_FRONTEND_DIST=$prebuiltFrontend" }

    Say 'Configuration (CMake)' 'Configuring (CMake)'
    try {
        Invoke-Native $cmake $configure
    } catch {
        if (-not $incremental) { throw }
        # A kept build tree can become unusable (Visual Studio or CMake updated): start again from scratch.
        Say 'La compilation précédente est inutilisable : recompilation complète' `
            'The previous build cannot be reused: full rebuild'
        Remove-Item -Recurse -Force -LiteralPath $BuildDir
        Invoke-Native $cmake $configure
    }

    Say 'Compilation' 'Building'
    Invoke-Native $cmake @('--build', $BuildDir, '--config', 'Release', '--parallel')

    $manifestPath = Join-Path $BuildDir 'install_manifest.txt'
    $previousFiles = Read-Manifest $manifestPath
    $legacy = @('scratch2arduino-webview.exe', 'scratch2arduino-native.exe') |
        Where-Object { Test-Path (Join-Path $InstallDir "bin\$_") }
    if (-not $incremental -and $legacy) {
        # Versions before 0.4 did not keep their build tree, so their file list is unknown: their
        # programs (two variants, Qt libraries) are removed with the folders they were installed in.
        Say 'Suppression de l''ancienne version (deux variantes)' 'Removing the previous version (two variants)'
        foreach ($folder in 'bin', 'share', 'lib') {
            Remove-Item -Recurse -Force -LiteralPath (Join-Path $InstallDir $folder) -ErrorAction SilentlyContinue
        }
    }
    Say "Installation dans $InstallDir" "Installing into $InstallDir"
    Invoke-Native $cmake @('--install', $BuildDir, '--config', 'Release')

    # Remove what the previous version installed and this one no longer does.
    $installed = @{}
    foreach ($file in Read-Manifest $manifestPath) { $installed[$file.ToLowerInvariant()] = $true }
    foreach ($file in $previousFiles) {
        if ($file -and -not $installed.ContainsKey($file.ToLowerInvariant()) -and (Test-Path -LiteralPath $file)) {
            Remove-Item -LiteralPath $file -Force
        }
    }

    Say 'Scratch2Arduino est installé !' 'Scratch2Arduino is installed!'
    exit 0
}
catch {
    Add-Content -Path $LogFile -Value "ERROR: $_" -Encoding UTF8
    Write-Host $_ -ForegroundColor Red
    Fail "La compilation a échoué. Journal : $LogFile" "The build failed. Log: $LogFile"
}
