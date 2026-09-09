param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot "htmsr_environment.local.json"),
    [switch]$CreateDefault,
    [switch]$NoPrompt,
    [switch]$Configure,
    [string]$Preset = "local-vs2022-x64-debug",
    [string]$QtRoot,
    [string]$OpenCvRoot,
    [string]$PclRoot,
    [string]$VtkRoot,
    [string]$EigenIncludeDir,
    [string]$MvsRoot,
    [string]$MvsRuntimeDir,
    [ValidateSet("ON", "OFF")]
    [string]$EnableHikCamera,
    [ValidateSet("ON", "OFF")]
    [string]$EnableVtkViewer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$userPresetPath = Join-Path $projectRoot "CMakeUserPresets.json"

function New-DefaultConfig {
    [ordered]@{
        qtRoot = "C:/ENVIORNMENT/qt/5.15.2/msvc2019_64"
        opencvRoot = "C:/ENVIORNMENT/opencv_450_vs2019"
        pclRoot = "C:/ENVIORNMENT/PCL/PCL 1.12.1"
        vtkRoot = ""
        eigenIncludeDir = "C:/ENVIORNMENT/ceresLib/Eigen"
        mvsRoot = "C:/ENVIORNMENT/MVS"
        mvsRuntimeDir = "C:/Program Files (x86)/Common Files/MVS/Runtime/Win64_x64"
        enableHikCamera = $true
        enableVtkViewer = $true
    }
}

function ConvertTo-CMakePath {
    param([string]$PathValue)

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }

    $expanded = [Environment]::ExpandEnvironmentVariables($PathValue.Trim())
    $absolute = [System.IO.Path]::GetFullPath($expanded)
    return $absolute.Replace("\", "/")
}

function Join-CMakePath {
    param(
        [string]$Root,
        [string]$Child
    )

    if ([string]::IsNullOrWhiteSpace($Root)) {
        return ""
    }

    return (Join-Path $Root $Child).Replace("\", "/")
}

function ConvertTo-CMakeBool {
    param($Value)

    if ($Value -is [bool]) {
        if ($Value) { return "ON" }
        return "OFF"
    }

    $text = [string]$Value
    if ($text.Equals("ON", [StringComparison]::OrdinalIgnoreCase) -or
        $text.Equals("TRUE", [StringComparison]::OrdinalIgnoreCase) -or
        $text.Equals("1", [StringComparison]::OrdinalIgnoreCase)) {
        return "ON"
    }

    return "OFF"
}

function Set-ConfigProperty {
    param(
        [pscustomobject]$Config,
        [string]$Name,
        $Value
    )

    if ([string]::IsNullOrWhiteSpace([string]$Value)) {
        return
    }

    if ($Config.PSObject.Properties.Name -contains $Name) {
        $Config.$Name = $Value
    } else {
        $Config | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function Get-ConfigProperty {
    param(
        [pscustomobject]$Config,
        [string]$Name,
        $Fallback
    )

    if ($Config.PSObject.Properties.Name -contains $Name) {
        return $Config.$Name
    }

    return $Fallback
}

function Test-HtmsrPath {
    param(
        [string]$Name,
        [string]$PathValue,
        [bool]$Required
    )

    if (Test-Path -LiteralPath $PathValue) {
        Write-Host ("[OK]      {0}: {1}" -f $Name, $PathValue)
        return $true
    }

    if ($Required) {
        Write-Warning ("[Missing] {0}: {1}" -f $Name, $PathValue)
    } else {
        Write-Host ("[Skip]    {0}: {1}" -f $Name, $PathValue)
    }
    return $false
}

function Copy-OrderedDictionary {
    param($Source)

    $copy = [ordered]@{}
    foreach ($key in $Source.Keys) {
        $copy[$key] = $Source[$key]
    }
    return $copy
}

$defaultConfig = [pscustomobject](New-DefaultConfig)

if ($CreateDefault -or -not (Test-Path -LiteralPath $ConfigPath)) {
    $configDirectory = Split-Path -Parent $ConfigPath
    if ($configDirectory -and -not (Test-Path -LiteralPath $configDirectory)) {
        New-Item -ItemType Directory -Path $configDirectory | Out-Null
    }

    $defaultConfig | ConvertTo-Json -Depth 5 | Set-Content -Path $ConfigPath -Encoding UTF8
    Write-Host "Created local environment config: $ConfigPath"

    if (-not $NoPrompt -and -not $CreateDefault) {
        Write-Host "Edit this JSON file first, then run this script again."
        exit 0
    }
}

$config = Get-Content -Path $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json

Set-ConfigProperty $config "qtRoot" $QtRoot
Set-ConfigProperty $config "opencvRoot" $OpenCvRoot
Set-ConfigProperty $config "pclRoot" $PclRoot
Set-ConfigProperty $config "vtkRoot" $VtkRoot
Set-ConfigProperty $config "eigenIncludeDir" $EigenIncludeDir
Set-ConfigProperty $config "mvsRoot" $MvsRoot
Set-ConfigProperty $config "mvsRuntimeDir" $MvsRuntimeDir
Set-ConfigProperty $config "enableHikCamera" $EnableHikCamera
Set-ConfigProperty $config "enableVtkViewer" $EnableVtkViewer

$qtRoot = ConvertTo-CMakePath (Get-ConfigProperty $config "qtRoot" $defaultConfig.qtRoot)
$opencvRoot = ConvertTo-CMakePath (Get-ConfigProperty $config "opencvRoot" $defaultConfig.opencvRoot)
$pclRoot = ConvertTo-CMakePath (Get-ConfigProperty $config "pclRoot" $defaultConfig.pclRoot)
$vtkRoot = ConvertTo-CMakePath (Get-ConfigProperty $config "vtkRoot" $defaultConfig.vtkRoot)
$eigenIncludeDir = ConvertTo-CMakePath (Get-ConfigProperty $config "eigenIncludeDir" $defaultConfig.eigenIncludeDir)
$mvsRoot = ConvertTo-CMakePath (Get-ConfigProperty $config "mvsRoot" $defaultConfig.mvsRoot)
$mvsRuntimeDir = ConvertTo-CMakePath (Get-ConfigProperty $config "mvsRuntimeDir" $defaultConfig.mvsRuntimeDir)
$hikCameraEnabled = ConvertTo-CMakeBool (Get-ConfigProperty $config "enableHikCamera" $defaultConfig.enableHikCamera)
$vtkViewerEnabled = ConvertTo-CMakeBool (Get-ConfigProperty $config "enableVtkViewer" $defaultConfig.enableVtkViewer)

$opencvDir = Join-CMakePath $opencvRoot "x64/vc16/lib"
$pclDir = Join-CMakePath $pclRoot "cmake"
$vtkDir = if ([string]::IsNullOrWhiteSpace($vtkRoot)) {
    Join-CMakePath $pclRoot "3rdParty/VTK/lib/cmake/vtk-9.1"
} else {
    Join-CMakePath $vtkRoot "lib/cmake/vtk-9.1"
}
$boostDir = Join-CMakePath $pclRoot "3rdParty/Boost/lib/cmake/Boost-1.78.0"
$qhullDir = Join-CMakePath $pclRoot "3rdParty/Qhull/lib/cmake/Qhull"
$qt5Dir = Join-CMakePath $qtRoot "lib/cmake/Qt5"
$mvsHeader = Join-CMakePath $mvsRoot "Development/Includes/MvCameraControl.h"
$mvsLibrary = Join-CMakePath $mvsRoot "Development/Libraries/win64/MvCameraControl.lib"
$mvsRuntimeDll = Join-CMakePath $mvsRuntimeDir "MvCameraControl.dll"

$prefixPaths = @(
    $qtRoot,
    $opencvDir,
    $pclRoot,
    $pclDir,
    $vtkDir,
    $boostDir,
    $qhullDir
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

$baseCacheVariables = [ordered]@{
    CMAKE_EXPORT_COMPILE_COMMANDS = "ON"
    HTMSR_ENABLE_VTK_VIEWER = $vtkViewerEnabled
    HTMSR_ENABLE_HIK_CAMERA = $hikCameraEnabled
    HIK_MVS_ROOT = $mvsRoot
    HIK_MVS_RUNTIME_DIR = $mvsRuntimeDir
    CMAKE_PREFIX_PATH = ($prefixPaths -join ";")
    OpenCV_DIR = $opencvDir
    PCL_DIR = $pclDir
    VTK_DIR = $vtkDir
    Qt5_DIR = $qt5Dir
    HTMSR_EIGEN_INCLUDE_DIR = $eigenIncludeDir
}

$debugCacheVariables = Copy-OrderedDictionary $baseCacheVariables
$debugCacheVariables["CMAKE_BUILD_TYPE"] = "Debug"

$releaseCacheVariables = [ordered]@{
    CMAKE_BUILD_TYPE = "Release"
}

$userPresets = [ordered]@{
    version = 5
    configurePresets = @(
        [ordered]@{
            name = "local-vs2022-x64-debug"
            displayName = "Local Visual Studio 2022 x64 Debug"
            inherits = "vs2022-x64-debug"
            binaryDir = '${sourceDir}/out/build/local-vs2022-x64-debug'
            cacheVariables = $debugCacheVariables
        },
        [ordered]@{
            name = "local-vs2022-x64-release"
            displayName = "Local Visual Studio 2022 x64 Release"
            inherits = "local-vs2022-x64-debug"
            binaryDir = '${sourceDir}/out/build/local-vs2022-x64-release'
            cacheVariables = $releaseCacheVariables
        }
    )
    buildPresets = @(
        [ordered]@{
            name = "local-debug"
            configurePreset = "local-vs2022-x64-debug"
        },
        [ordered]@{
            name = "local-release"
            configurePreset = "local-vs2022-x64-release"
        }
    )
}

$userPresets | ConvertTo-Json -Depth 10 | Set-Content -Path $userPresetPath -Encoding UTF8
Write-Host "Created CMake user presets: $userPresetPath"

Write-Host ""
Write-Host "Environment path check:"
$missingRequired = 0
if (-not (Test-HtmsrPath "Qt5Config.cmake" (Join-CMakePath $qt5Dir "Qt5Config.cmake") $true)) { $missingRequired++ }
if (-not (Test-HtmsrPath "OpenCVConfig.cmake" (Join-CMakePath $opencvDir "OpenCVConfig.cmake") $true)) { $missingRequired++ }
if (-not (Test-HtmsrPath "PCLConfig.cmake" (Join-CMakePath $pclDir "PCLConfig.cmake") $true)) { $missingRequired++ }
if (-not (Test-HtmsrPath "Eigen/Core" (Join-CMakePath $eigenIncludeDir "Eigen/Core") $true)) { $missingRequired++ }
Test-HtmsrPath "vtk-config.cmake (optional embedded viewer)" (Join-CMakePath $vtkDir "vtk-config.cmake") $false | Out-Null

if ($hikCameraEnabled -eq "ON") {
    if (-not (Test-HtmsrPath "MvCameraControl.h" $mvsHeader $true)) { $missingRequired++ }
    if (-not (Test-HtmsrPath "MvCameraControl.lib" $mvsLibrary $true)) { $missingRequired++ }
    if (-not (Test-HtmsrPath "MvCameraControl.dll" $mvsRuntimeDll $true)) { $missingRequired++ }
} else {
    Test-HtmsrPath "MvCameraControl.h" $mvsHeader $false | Out-Null
    Test-HtmsrPath "MvCameraControl.lib" $mvsLibrary $false | Out-Null
    Test-HtmsrPath "MvCameraControl.dll" $mvsRuntimeDll $false | Out-Null
}

Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. In Visual Studio, select configure preset: local-vs2022-x64-debug"
Write-Host "  2. Or run: cmake --preset local-vs2022-x64-debug"
Write-Host "  3. Build with: cmake --build --preset local-debug"

if ($missingRequired -gt 0) {
    Write-Warning "Missing required paths: $missingRequired. Edit $ConfigPath and run this script again."
    if ($Configure) {
        Write-Warning "Skipped CMake configure because required paths are missing."
    }
    exit 0
}

if ($Configure) {
    Write-Host ""
    Write-Host "Running CMake configure: cmake --preset $Preset"
    Push-Location $projectRoot
    try {
        & cmake --preset $Preset
    } finally {
        Pop-Location
    }
}
