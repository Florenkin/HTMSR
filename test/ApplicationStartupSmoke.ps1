# Manual startup smoke check. It starts the application (including normal camera
# discovery/preview), performs no scan actions, and stops only its own process.
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [string]$LogDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Executable = [IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($Executable))
if (-not (Test-Path -LiteralPath $Executable)) { throw "Executable missing: $Executable" }
$work = Join-Path $projectRoot ('out/startup-review/startup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
if (-not $LogDirectory) { $LogDirectory = Join-Path $projectRoot 'log' }
$started = Get-Date
$startupLog = $null
$process = Start-Process -FilePath $Executable -WorkingDirectory $work -WindowStyle Hidden -PassThru
try {
    $ready = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process) {
            $process.Refresh()
            if ($process.HasExited) { throw "Application exited during startup: $($process.ExitCode)" }
        }
        if (Test-Path -LiteralPath $LogDirectory) {
            foreach ($log in Get-ChildItem -LiteralPath $LogDirectory -Filter '*.log') {
                if ($log.CreationTime -lt $started) { continue }
                if ((Get-Content -LiteralPath $log.FullName -Raw -Encoding UTF8) -match 'HTMSR started\.') {
                    $ready = $true
                    $startupLog = $log.FullName
                }
            }
        }
        if ($ready -and $process) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw "No successful startup record. Inspect $work" }
    Start-Sleep -Seconds 2
    $process.Refresh()
    if ($process.HasExited) { throw "Application failed just after startup: $($process.ExitCode)" }
    Copy-Item -LiteralPath $startupLog -Destination (Join-Path $work 'startup.log')
    Write-Output "PASS: application initialized and remained running: $Executable"
    Write-Output "Startup logs: $work"
} finally {
    if ($process) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
