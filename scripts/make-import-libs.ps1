# Generates MSVC import libraries (.lib) from the DLLs of an installed OBS Studio,
# so the plugin links against exactly the libobs / Qt / FFmpeg the user runs.
param(
    [string]$ObsDir = "C:\Program Files\obs-studio",
    [string]$OutDir = (Join-Path $PSScriptRoot "..\deps\importlibs")
)

$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" | Sort-Object Name -Descending | Select-Object -First 1
$bin = Join-Path $msvc.FullName "bin\Hostx64\x64"
$dumpbin = Join-Path $bin "dumpbin.exe"
$lib = Join-Path $bin "lib.exe"

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# DLL file name -> import library name
$dlls = [ordered]@{
    "obs.dll"              = "obs"
    "obs-frontend-api.dll" = "obs-frontend-api"
    "Qt6Core.dll"          = "Qt6Core"
    "Qt6Gui.dll"           = "Qt6Gui"
    "Qt6Widgets.dll"       = "Qt6Widgets"
    "Qt6Svg.dll"           = "Qt6Svg"
}
$binDir = Join-Path $ObsDir "bin\64bit"
foreach ($f in Get-ChildItem $binDir -Filter "av*.dll") { $dlls[$f.Name] = ($f.BaseName -replace "-\d+$", "") }
foreach ($f in Get-ChildItem $binDir -Filter "sw*.dll") { $dlls[$f.Name] = ($f.BaseName -replace "-\d+$", "") }

foreach ($dll in $dlls.Keys) {
    $name = $dlls[$dll]
    $path = Join-Path $binDir $dll
    if (-not (Test-Path $path)) { throw "Missing $path" }

    $exports = & $dumpbin /nologo /exports $path |
        ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+(?:[0-9A-F]{8}\s+)?(\S+)') { $Matches[1] } } |
        Where-Object { $_ -ne "[NONAME]" }

    $def = Join-Path $OutDir "$name.def"
    @("LIBRARY `"$dll`"", "EXPORTS") + ($exports | ForEach-Object { "    $_" }) | Set-Content -Encoding ascii $def
    $out = Join-Path $OutDir "$name.lib"
    & $lib /nologo "/def:$def" /machine:x64 "/out:$out" | Out-Null
    if (-not (Test-Path $out)) { throw "lib.exe failed for $dll" }
    Write-Host ("{0,-24} -> {1}.lib ({2} exports)" -f $dll, $name, $exports.Count)
}
