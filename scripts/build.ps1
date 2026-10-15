# Builds the plugin and installs it to %ProgramData%\obs-studio\plugins\instant-clip.
# Close OBS first: Windows locks the loaded DLL.
param([switch]$NoInstall)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

& (Join-Path $PSScriptRoot "fetch-deps.ps1") -DepsDir (Join-Path $root "deps")

& (Join-Path $PSScriptRoot "embed-secrets.ps1") -Out (Join-Path $root "build\gen\embedded-secrets.inc")
& $cmake -S $root -B (Join-Path $root "build") -G "Visual Studio 17 2022" -A x64
& $cmake --build (Join-Path $root "build") --config RelWithDebInfo
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

if (-not $NoInstall) {
    if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw "Close OBS before installing." }
    & $cmake --install (Join-Path $root "build") --config RelWithDebInfo
}
