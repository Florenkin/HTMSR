param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot "htmsr_environment.local.json"),
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$BuildDir = "",
    [string]$PackageDir = "",
    [switch]$Clean,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$defaultVsDevCmd = "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat"
$defaultCMake = "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"

function ConvertTo-NativePath {
    param([string]$PathValue)

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }

    $expanded = [Environment]::ExpandEnvironmentVariables($PathValue.Trim())
    return [System.IO.Path]::GetFullPath($expanded)
}

function Join-NativePath {
    param(
        [string]$Root,
        [string]$Child
    )

    if ([string]::IsNullOrWhiteSpace($Root)) {
        return ""
    }

    return [System.IO.Path]::GetFullPath((Join-Path $Root $Child))
}

function Get-ConfigValue {
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

function Test-RequiredPath {
    param(
        [string]$Name,
        [string]$PathValue
    )

    if (-not (Test-Path -LiteralPath $PathValue)) {
        throw "Required path was not found: $Name => $PathValue"
    }

    Write-Host ("[OK] {0}: {1}" -f $Name, $PathValue)
}

function Copy-FileIfExists {
    param(
        [string]$Source,
        [string]$DestinationDirectory
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        return $false
    }

    if (-not (Test-Path -LiteralPath $DestinationDirectory)) {
        New-Item -ItemType Directory -Path $DestinationDirectory | Out-Null
    }

    Copy-Item -LiteralPath $Source -Destination $DestinationDirectory -Force
    return $true
}

function Copy-DirectoryFiles {
    param(
        [string]$SourceDirectory,
        [string]$Pattern,
        [string]$DestinationDirectory,
        [switch]$Recurse,
        [switch]$Optional
    )

    if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
        if ($Optional) {
            Write-Host ("[Skip] Directory not found: {0}" -f $SourceDirectory)
            return 0
        }

        throw "Required directory was not found: $SourceDirectory"
    }

    if (-not (Test-Path -LiteralPath $DestinationDirectory)) {
        New-Item -ItemType Directory -Path $DestinationDirectory | Out-Null
    }

    $files = @(Get-ChildItem -LiteralPath $SourceDirectory -Filter $Pattern -File -Recurse:$Recurse)
    foreach ($file in $files) {
        Copy-Item -LiteralPath $file.FullName -Destination $DestinationDirectory -Force
    }

    Write-Host ("[Copy] {0} file(s) from {1}" -f $files.Count, $SourceDirectory)
    return $files.Count
}

function Copy-PluginDirectory {
    param(
        [string]$SourceDirectory,
        [string]$DestinationDirectory
    )

    if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
        Write-Host ("[Skip] Qt plugin directory not found: {0}" -f $SourceDirectory)
        return
    }

    if (Test-Path -LiteralPath $DestinationDirectory) {
        Remove-Item -LiteralPath $DestinationDirectory -Recurse -Force
    }

    Copy-Item -LiteralPath $SourceDirectory -Destination $DestinationDirectory -Recurse -Force
    Write-Host ("[Copy] Qt plugin directory: {0}" -f $SourceDirectory)
}

function Copy-MsvcRuntime {
    param(
        [string]$DestinationDirectory,
        [string]$ConfigurationName
    )

    if ($ConfigurationName -eq "Debug") {
        Write-Host "[Skip] MSVC debug runtime is not redistributable. Use this Debug package only on development machines."
        return
    }

    $redistRoot = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC"
    if (-not (Test-Path -LiteralPath $redistRoot -PathType Container)) {
        Write-Host ("[Skip] MSVC redist root not found: {0}" -f $redistRoot)
        return
    }

    $crtDirectories = @(Get-ChildItem -LiteralPath $redistRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "x64/Microsoft.VC143.CRT" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container })

    if ($crtDirectories.Count -eq 0) {
        Write-Host ("[Skip] MSVC x64 CRT directory not found below: {0}" -f $redistRoot)
        return
    }

    $runtimeDirectory = $crtDirectories[0]
    foreach ($dll in @("concrt140.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
        Copy-FileIfExists (Join-NativePath $runtimeDirectory $dll) $DestinationDirectory | Out-Null
    }
    Write-Host ("[Copy] MSVC runtime from {0}" -f $runtimeDirectory)
}

function Invoke-CMake {
    param([string[]]$Arguments)

    $cmakePath = $null
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmakeCommand) {
        $cmakePath = $cmakeCommand.Source
    } elseif (Test-Path -LiteralPath $defaultCMake) {
        $cmakePath = $defaultCMake
    } else {
        throw "CMake was not found. Install CMake or run this script from Visual Studio Developer PowerShell."
    }

    if (Test-Path -LiteralPath $defaultVsDevCmd) {
        $quotedArgs = ($Arguments | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join " "
        $command = 'call "' + $defaultVsDevCmd + '" -arch=x64 -host_arch=x64 >nul && "' + $cmakePath + '" ' + $quotedArgs
        & cmd.exe /c $command
    } else {
        & $cmakePath @Arguments
    }

    if ($LASTEXITCODE -ne 0) {
        throw "CMake command failed: cmake $($Arguments -join ' ')"
    }
}

function Get-DumpbinPath {
    $dumpbinCommand = Get-Command dumpbin -ErrorAction SilentlyContinue
    if ($dumpbinCommand) {
        return $dumpbinCommand.Source
    }

    $candidate = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/dumpbin.exe"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return $candidate
    }

    return ""
}

function Get-ExecutableDependencies {
    param([string]$ExecutablePath)

    $dumpbinPath = Get-DumpbinPath
    if ([string]::IsNullOrWhiteSpace($dumpbinPath)) {
        Write-Host "[Skip] dumpbin was not found. Dependency validation skipped."
        return @()
    }

    if (Test-Path -LiteralPath $defaultVsDevCmd) {
        $command = 'call "' + $defaultVsDevCmd + '" -arch=x64 -host_arch=x64 >nul && "' + $dumpbinPath + '" /dependents "' + $ExecutablePath + '"'
        $output = & cmd.exe /c $command
    } else {
        $output = & $dumpbinPath /dependents $ExecutablePath
    }

    $dependencies = @()
    foreach ($line in $output) {
        $text = $line.Trim()
        if ($text -match "^[A-Za-z0-9_.+-]+\.dll$") {
            $dependencies += $text
        }
    }

    return $dependencies
}

function Test-ReleaseExecutableDependencies {
    param([string]$ExecutablePath)

    $dependencies = @(Get-ExecutableDependencies $ExecutablePath)
    if ($dependencies.Count -eq 0) {
        return
    }

    $debugDependencies = @($dependencies | Where-Object {
        $_ -match "Qt5.*d\.dll$" -or
        $_ -match "opencv_.*\d+d\.dll$" -or
        $_ -match "pcl_.*d\.dll$" -or
        $_ -match "vtk.*-9\.1d\.dll$" -or
        $_ -match "MSVCP\d+D\.dll$" -or
        $_ -match "VCRUNTIME\d+.*D\.dll$" -or
        $_ -ieq "ucrtbased.dll"
    })

    if ($debugDependencies.Count -gt 0) {
        throw "Release executable depends on Debug DLL(s): $($debugDependencies -join ', '). Reconfigure Release without Debug dependencies."
    }

    Write-Host "[OK] Release dependency check passed."
}

function Remove-DebugRuntimeFiles {
    param([string]$Directory)

    $patterns = @(
        "Qt5*d.dll",
        "opencv_*d.dll",
        "pcl_*d.dll",
        "vtk*-9.1d.dll",
        "MSVCP*D.dll",
        "VCRUNTIME*D.dll",
        "ucrtbased.dll",
        "flann-gd.dll",
        "flann_cpp-gd.dll"
    )

    $removed = 0
    foreach ($pattern in $patterns) {
        $files = @(Get-ChildItem -LiteralPath $Directory -Recurse -Filter $pattern -File -ErrorAction SilentlyContinue)
        foreach ($file in $files) {
            Remove-Item -LiteralPath $file.FullName -Force
            $removed++
        }
    }

    Write-Host ("[Clean] Removed {0} debug runtime file(s) from package." -f $removed)
}

function Test-DebugExecutableDependencies {
    param([string]$ExecutablePath)

    $dependencies = @(Get-ExecutableDependencies $ExecutablePath)
    if ($dependencies.Count -eq 0) {
        return
    }

    $expectedDebugDependencies = @($dependencies | Where-Object {
        $_ -match "Qt5.*d\.dll$" -or
        $_ -match "opencv_.*\d+d\.dll$" -or
        $_ -match "pcl_.*d\.dll$" -or
        $_ -match "vtk.*-9\.1d\.dll$" -or
        $_ -match "MSVCP\d+D\.dll$" -or
        $_ -match "VCRUNTIME\d+.*D\.dll$" -or
        $_ -ieq "ucrtbased.dll"
    })

    if ($expectedDebugDependencies.Count -eq 0) {
        Write-Host "[Warn] Debug executable did not report Debug DLL dependencies. Check whether BuildDir points to a Debug build."
    } else {
        Write-Host "[OK] Debug dependency check found Debug runtime dependencies."
    }
}

function ConvertTo-Boolean {
    param($Value)

    if ($Value -is [bool]) {
        return $Value
    }

    $text = [string]$Value
    return $text.Equals("true", [StringComparison]::OrdinalIgnoreCase) -or
        $text.Equals("on", [StringComparison]::OrdinalIgnoreCase) -or
        $text.Equals("1", [StringComparison]::OrdinalIgnoreCase)
}

if (-not (Test-Path -LiteralPath $ConfigPath)) {
    throw "Local environment config was not found: $ConfigPath. Run scripts/Configure-HtmsrEnvironment.ps1 first."
}

$config = Get-Content -Path $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json

$qtRoot = ConvertTo-NativePath (Get-ConfigValue $config "qtRoot" "C:/ENVIORNMENT/qt/5.15.2/msvc2019_64")
$opencvRoot = ConvertTo-NativePath (Get-ConfigValue $config "opencvRoot" "C:/ENVIORNMENT/opencv_450_vs2019")
$pclRoot = ConvertTo-NativePath (Get-ConfigValue $config "pclRoot" "C:/ENVIORNMENT/PCL/PCL 1.12.1")
$vtkRoot = ConvertTo-NativePath (Get-ConfigValue $config "vtkRoot" "")
$mvsRuntimeDir = ConvertTo-NativePath (Get-ConfigValue $config "mvsRuntimeDir" "C:/Program Files (x86)/Common Files/MVS/Runtime/Win64_x64")
$hikCameraEnabled = ConvertTo-Boolean (Get-ConfigValue $config "enableHikCamera" $true)
$vtkBinDir = if ([string]::IsNullOrWhiteSpace($vtkRoot)) {
    Join-NativePath $pclRoot "3rdParty/VTK/bin"
} else {
    Join-NativePath $vtkRoot "bin"
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    if ($Configuration -eq "Debug") {
        $BuildDir = Join-Path $projectRoot "out/build/local-vs2022-x64-debug"
    } else {
        $BuildDir = Join-Path $projectRoot "out/build/local-vs2022-x64-release"
    }
}

if ([string]::IsNullOrWhiteSpace($PackageDir)) {
    if ($Configuration -eq "Debug") {
        $PackageDir = Join-Path $projectRoot "out/package/HTMSR_debug"
    } else {
        $PackageDir = Join-Path $projectRoot "out/package/HTMSR_release"
    }
}

$configurePreset = if ($Configuration -eq "Debug") { "local-vs2022-x64-debug" } else { "local-vs2022-x64-release" }
$buildPreset = if ($Configuration -eq "Debug") { "local-debug" } else { "local-release" }

$buildDirPath = ConvertTo-NativePath $BuildDir
$packageDirPath = ConvertTo-NativePath $PackageDir
$exePath = Join-NativePath $buildDirPath "htmsr_app.exe"
$packageRoot = ConvertTo-NativePath (Join-Path $projectRoot "out/package")

Write-Host ("HTMSR {0} package" -f $Configuration)
Write-Host ("Project: {0}" -f $projectRoot)
Write-Host ("Build:   {0}" -f $buildDirPath)
Write-Host ("Package: {0}" -f $packageDirPath)
Write-Host ""

Test-RequiredPath "Qt root" $qtRoot
Test-RequiredPath "OpenCV bin" (Join-NativePath $opencvRoot "x64/vc16/bin")
Test-RequiredPath "PCL bin" (Join-NativePath $pclRoot "bin")
Write-Host ("[Info] VTK bin source: {0}" -f $vtkBinDir)
if ($hikCameraEnabled) {
    Test-RequiredPath "MVS runtime" $mvsRuntimeDir
}

if (-not $SkipBuild) {
    Write-Host ""
    Write-Host ("Configuring and building local {0}..." -f $Configuration)
    Push-Location $projectRoot
    try {
        if ($Configuration -eq "Release") {
            Invoke-CMake @("--preset", $configurePreset, "-DHTMSR_ENABLE_VTK_VIEWER=OFF")
        } else {
            Invoke-CMake @("--preset", $configurePreset, "-DHTMSR_ENABLE_VTK_VIEWER=ON")
        }
        Invoke-CMake @("--build", "--preset", $buildPreset)
    } finally {
        Pop-Location
    }
}

if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    throw "$Configuration executable was not found: $exePath. Build local $Configuration first or remove -SkipBuild."
}

if ($Configuration -eq "Release") {
    Test-ReleaseExecutableDependencies $exePath
} else {
    Test-DebugExecutableDependencies $exePath
}

if ($Clean -and (Test-Path -LiteralPath $packageDirPath)) {
    $resolvedPackage = (Resolve-Path -LiteralPath $packageDirPath).Path
    $resolvedPackageRoot = if (Test-Path -LiteralPath $packageRoot) { (Resolve-Path -LiteralPath $packageRoot).Path } else { $packageRoot }
    if (-not $resolvedPackage.StartsWith($resolvedPackageRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a package directory outside out/package: $resolvedPackage"
    }

    Remove-Item -LiteralPath $resolvedPackage -Recurse -Force
    Write-Host ("[Clean] {0}" -f $resolvedPackage)
}

if (-not (Test-Path -LiteralPath $packageDirPath)) {
    New-Item -ItemType Directory -Path $packageDirPath | Out-Null
}

Copy-Item -LiteralPath $exePath -Destination $packageDirPath -Force
Write-Host ("[Copy] htmsr_app.exe")

$windeployqt = Join-NativePath $qtRoot "bin/windeployqt.exe"
if (Test-Path -LiteralPath $windeployqt -PathType Leaf) {
    Write-Host "Running windeployqt..."
    $qtMode = if ($Configuration -eq "Debug") { "--debug" } else { "--release" }
    & $windeployqt $qtMode --compiler-runtime --dir $packageDirPath (Join-NativePath $packageDirPath "htmsr_app.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed."
    }
} else {
    Write-Host "[Fallback] windeployqt.exe not found. Copying required Qt5 runtime files manually."
    $qtBin = Join-NativePath $qtRoot "bin"
    if ($Configuration -eq "Debug") {
        $qtDlls = @(
            "Qt5Cored.dll",
            "Qt5Guid.dll",
            "Qt5Widgetsd.dll",
            "Qt5Concurrentd.dll",
            "Qt5OpenGLd.dll",
            "libEGLd.dll",
            "libGLESv2d.dll"
        )
    } else {
        $qtDlls = @(
            "Qt5Core.dll",
            "Qt5Gui.dll",
            "Qt5Widgets.dll",
            "Qt5Concurrent.dll",
            "Qt5OpenGL.dll",
            "libEGL.dll",
            "libGLESv2.dll"
        )
    }
    foreach ($dll in $qtDlls) {
        Copy-FileIfExists (Join-NativePath $qtBin $dll) $packageDirPath | Out-Null
    }

    Copy-PluginDirectory (Join-NativePath $qtRoot "plugins/platforms") (Join-NativePath $packageDirPath "platforms")
    Copy-PluginDirectory (Join-NativePath $qtRoot "plugins/imageformats") (Join-NativePath $packageDirPath "imageformats")
    Copy-PluginDirectory (Join-NativePath $qtRoot "plugins/styles") (Join-NativePath $packageDirPath "styles")
}

Copy-DirectoryFiles (Join-NativePath $opencvRoot "x64/vc16/bin") "*.dll" $packageDirPath | Out-Null
Copy-DirectoryFiles (Join-NativePath $pclRoot "bin") "*.dll" $packageDirPath | Out-Null
Copy-DirectoryFiles $vtkBinDir "*.dll" $packageDirPath -Optional | Out-Null
Copy-DirectoryFiles (Join-NativePath $pclRoot "3rdParty/FLANN/bin") "*.dll" $packageDirPath -Optional | Out-Null
Copy-DirectoryFiles (Join-NativePath $pclRoot "3rdParty/Qhull/bin") "*.dll" $packageDirPath -Optional | Out-Null
Copy-DirectoryFiles (Join-NativePath $pclRoot "3rdParty/Boost/lib") "*.dll" $packageDirPath -Optional | Out-Null
Copy-DirectoryFiles (Join-NativePath $pclRoot "3rdParty/OpenNI2/Redist") "*.dll" $packageDirPath -Optional | Out-Null
Copy-MsvcRuntime $packageDirPath $Configuration

if ($hikCameraEnabled) {
    foreach ($pattern in @("*.dll", "*.cti", "*.ini", "*.manifest")) {
        Copy-DirectoryFiles $mvsRuntimeDir $pattern $packageDirPath -Optional | Out-Null
    }
}
if ($Configuration -eq "Release") {
    Remove-DebugRuntimeFiles $packageDirPath
}

$readmePath = Join-NativePath $packageDirPath "README_run.txt"
@"
HTMSR $Configuration Package

Run:
  htmsr_app.exe

Notes:
  - This package is generated from $Configuration build output.
  - Keep platforms/qwindows.dll in the platforms directory.
  - Hik camera runtime files are included when enableHikCamera is true.
  - If the target computer cannot detect Hik cameras, install Hikrobot MVS client/runtime and verify cameras in MVS first.
  - Debug packages are only for development machines with Visual Studio debug runtime installed.
"@ | Set-Content -Path $readmePath -Encoding UTF8

$dllCount = (Get-ChildItem -LiteralPath $packageDirPath -Filter "*.dll" -File -ErrorAction SilentlyContinue).Count
$pluginCount = (Get-ChildItem -LiteralPath $packageDirPath -Recurse -File -ErrorAction SilentlyContinue).Count

Write-Host ""
Write-Host "Package completed."
Write-Host ("Directory: {0}" -f $packageDirPath)
Write-Host ("Root DLL count: {0}" -f $dllCount)
Write-Host ("Total file count: {0}" -f $pluginCount)
Write-Host ("Run: {0}" -f (Join-NativePath $packageDirPath "htmsr_app.exe"))
