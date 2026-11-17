# Runs Scratch2Arduino_installer.exe silently and shows its build log while it runs, so that
# the CI step does not stay silent for many minutes. Exits with the installer exit code.
param(
    [Parameter(Mandatory = $true)][string]$SetupLog,
    # Tasks to check or uncheck ("buildfromsource", "!buildfromsource"), merged with those of the
    # previous installation, which the installer remembers. Default: the remembered ones.
    [string]$Tasks = '',
    # Shows the installer window (/SILENT: progress only, no questions) and fails when the window
    # stops responding while the build runs (the window froze during the build up to 1.2.0).
    [switch]$CheckResponsive
)

$installer = 'installer\windows\Output\Scratch2Arduino_installer.exe'
$buildLog = Join-Path $env:ProgramFiles 'Scratch2Arduino\install-log.txt'
$watch = [Diagnostics.Stopwatch]::StartNew()
$mode = if ($CheckResponsive) { '/SILENT' } else { '/VERYSILENT' }
$arguments = @($mode, '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$SetupLog")
if ($Tasks) { $arguments += "/MERGETASKS=$Tasks" }
$process = Start-Process -FilePath $installer -PassThru -ArgumentList $arguments
$null = $process.Handle # without it, ExitCode stays empty once the process has exited

$shown = 0      # lines of the build log already printed
$lastNews = 0   # seconds, last time something was printed

function Show-NewLines {
    if (-not (Test-Path $buildLog)) { return }
    try {
        # Shared read: the build script appends to the file while we read it.
        $stream = [IO.File]::Open($buildLog, 'Open', 'Read', 'ReadWrite')
        $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8)
        $lines = @($reader.ReadToEnd() -split "`r?`n")
        $reader.Close()
    } catch {
        return # being replaced: next time
    }
    if ($lines.Count -gt 0 -and $lines[-1] -eq '') { $lines = @($lines | Select-Object -First ($lines.Count - 1)) }
    if ($lines.Count -lt $script:shown) { $script:shown = 0 } # the log was started again
    for ($i = $script:shown; $i -lt $lines.Count; $i++) { Write-Host $lines[$i] }
    if ($lines.Count -gt $script:shown) { $script:lastNews = [int]$watch.Elapsed.TotalSeconds }
    $script:shown = $lines.Count
}

# The wizard runs in a child process (<temp>\is-*.tmp\Scratch2Arduino_installer.tmp) that the
# installer starts: Process.Responding asks its window (5 s timeout).
$checks = 0          # times the wizard window was asked
$notResponding = 0   # checks in a row without an answer
function Test-Responsive {
    $wizard = Get-CimInstance Win32_Process -Filter "ParentProcessId = $($process.Id)" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like '*.tmp' } | Select-Object -First 1
    if (-not $wizard) { return }
    $p = Get-Process -Id $wizard.ProcessId -ErrorAction SilentlyContinue
    if (-not $p -or $p.MainWindowHandle -eq [IntPtr]::Zero) { return }
    # Only while the build runs: it is the part that froze.
    if (-not (Test-Path (Join-Path $env:ProgramFiles 'Scratch2Arduino\build.pid'))) { return }
    $script:checks++
    if ($p.Responding) {
        $script:notResponding = 0
    } else {
        $script:notResponding++
        Write-Host "::warning::the installer window does not respond ($($script:notResponding))"
        if ($script:notResponding -ge 3) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Stop-Process -Id $wizard.ProcessId -Force -ErrorAction SilentlyContinue
            Write-Host "::error::the installer window stopped responding during the build"
            exit 1
        }
    }
}

while (-not $process.WaitForExit(5000)) {
    if ($CheckResponsive) { Test-Responsive }
    Show-NewLines
    $elapsed = [int]$watch.Elapsed.TotalSeconds
    if ($elapsed - $script:lastNews -ge 60) {
        Write-Host "... still running ($([math]::Floor($elapsed / 60)) min $($elapsed % 60) s)"
        $script:lastNews = $elapsed
    }
}
Show-NewLines
if ($CheckResponsive) {
    Write-Host "The installer window answered $checks check(s) during the build"
    if ($checks -eq 0) { Write-Host '::error::the installer window was never checked during the build'; exit 1 }
}
Write-Host "Installer finished in $([int]$watch.Elapsed.TotalSeconds) s with exit code $($process.ExitCode)"
exit $process.ExitCode
