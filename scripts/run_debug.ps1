$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "out/build/vs2022-x64-debug/htmsr_app.exe"

if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe"
}

$runtimePaths = @(
    "C:\ENVIORNMENT\qt\6.11.1\msvc2022_64\bin",
    "C:\ENVIORNMENT\opencv_450_vs2019\x64\vc16\bin",
    "C:\ENVIORNMENT\PCL\PCL 1.12.1\bin",
    "C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\VTK\bin",
    "C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\FLANN\bin",
    "C:\ENVIORNMENT\PCL\PCL 1.12.1\3rdParty\Qhull\bin",
    "C:\Program Files\OpenNI2\Redist"
)

$env:PATH = ($runtimePaths -join ";") + ";" + $env:PATH
& $exe
