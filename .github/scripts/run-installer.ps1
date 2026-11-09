# Runs Scratch2Arduino_installer.exe silently and shows its build log while it runs, so that
# the CI step does not stay silent for many minutes. Exits with the installer exit code.
param(
    [Parameter(Mandatory = $true)][string]$SetupLog
)

$installer = 'installer\windows\Output\Scratch2Arduino_installer.exe'
$buildLog = Join-Path $env:ProgramFiles 'Scratch2Arduino\install-log.txt'
$watch = [Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $installer -PassThru `
    -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$SetupLog"
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

while (-not $process.WaitForExit(5000)) {
    Show-NewLines
    $elapsed = [int]$watch.Elapsed.TotalSeconds
    if ($elapsed - $script:lastNews -ge 60) {
        Write-Host "... still running ($([math]::Floor($elapsed / 60)) min $($elapsed % 60) s)"
        $script:lastNews = $elapsed
    }
}
Show-NewLines
Write-Host "Installer finished in $([int]$watch.Elapsed.TotalSeconds) s with exit code $($process.ExitCode)"
exit $process.ExitCode
