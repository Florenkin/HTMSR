# 检查发布包在清空开发机 PATH 后，使用相邻 config 启动并正常关闭。
param([Parameter(Mandatory = $true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$folder = Split-Path -LiteralPath $exe
if (-not (Test-Path -LiteralPath (Join-Path $folder 'config/paths.ini'))) {
    throw 'This check requires a prepared portable package with config/paths.ini.'
}
$logs = Join-Path $folder 'log'
$started = Get-Date
$info = [Diagnostics.ProcessStartInfo]::new($exe)
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$info.WorkingDirectory = [IO.Path]::GetTempPath()
$info.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
foreach ($key in @('HTMSR_CONFIG_DIR','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM')) {
    $info.Environment.Remove($key) | Out-Null
}
$process = [Diagnostics.Process]::Start($info)
try {
    $ready = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        $process.Refresh()
        if ($process.HasExited) { throw "Portable application exited: $($process.ExitCode)" }
        if (Test-Path -LiteralPath $logs) {
            foreach ($file in Get-ChildItem -LiteralPath $logs -Filter '*.log') {
                if ($file.LastWriteTime -ge $started -and
                    (Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8) -match 'HTMSR started\.') {
                    $ready = $true
                }
            }
        }
        if ($ready -and $process.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw 'No startup log in the package log directory; check portable config and DLLs.' }
    if ($process.MainWindowHandle -eq [IntPtr]::Zero -or -not $process.CloseMainWindow()) {
        throw 'Could not request a normal window close.'
    }
    if (-not $process.WaitForExit(15000)) { throw 'Normal close timed out.' }
    if ($process.ExitCode -ne 0) { throw "Application close failed: $($process.ExitCode)" }
    'PASS: portable startup with clean PATH, adjacent config, unrelated working directory and normal close'
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
