# Downloads the headers the build needs, matched to the OBS Studio installed on this PC,
# then generates import libraries from its DLLs. Safe to re-run; finished steps are skipped.
#   deps\obs-studio   libobs + frontend API headers (sparse git checkout)
#   deps\ffmpeg       FFmpeg headers (sparse git checkout)
#   deps\qt6          Qt headers + moc from the obs-deps package OBS was built with
#   deps\importlibs   .lib files generated from the installed DLLs
param(
    [string]$ObsDir = "C:\Program Files\obs-studio",
    [string]$DepsDir = (Join-Path $PSScriptRoot "..\deps")
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $DepsDir | Out-Null
$DepsDir = (Resolve-Path $DepsDir).Path
$bin = Join-Path $ObsDir "bin\64bit"
if (-not (Test-Path (Join-Path $bin "obs64.exe"))) { throw "OBS Studio not found in $ObsDir (use -ObsDir)." }

function Run([scriptblock]$cmd) {
    $ErrorActionPreference = "Continue"
    & $cmd 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match "error|fatal" }
    if ($LASTEXITCODE -ne 0) { throw "Failed: $cmd" }
}

# Versions of the installed OBS and the FFmpeg it ships.
$obsVersion = (Get-Item (Join-Path $bin "obs64.exe")).VersionInfo.ProductVersion
$ffmpegDll = Get-ChildItem $bin -Filter "avcodec-*.dll" | Select-Object -First 1
$ffmpegTag = (Get-Item $ffmpegDll.FullName).VersionInfo.ProductVersion
Write-Host "OBS $obsVersion, FFmpeg $ffmpegTag"

# 1. OBS headers
$obsSrc = Join-Path $DepsDir "obs-studio"
if (-not (Test-Path (Join-Path $obsSrc "libobs\obs.h"))) {
    Write-Host "Fetching OBS $obsVersion headers..."
    Run { git -c advice.detachedHead=false clone -q --depth 1 --branch $obsVersion --filter=blob:none --sparse https://github.com/obsproject/obs-studio.git $obsSrc }
    Run { git -C $obsSrc sparse-checkout set libobs frontend/api }
}

# 2. FFmpeg headers
$ffSrc = Join-Path $DepsDir "ffmpeg"
if (-not (Test-Path (Join-Path $ffSrc "libavformat\avformat.h"))) {
    Write-Host "Fetching FFmpeg $ffmpegTag headers..."
    Run { git -c advice.detachedHead=false clone -q --depth 1 --branch $ffmpegTag --filter=blob:none --sparse https://github.com/FFmpeg/FFmpeg.git $ffSrc }
    Run { git -C $ffSrc sparse-checkout set --no-cone "/libavutil/*.h" "/libavcodec/*.h" "/libavformat/*.h" "/libswscale/*.h" "/libswresample/*.h" }
}

# 3. Qt headers + moc, from the obs-deps release named in OBS's CMakePresets.json
$qtDir = Join-Path $DepsDir "qt6"
if (-not (Test-Path (Join-Path $qtDir "bin\moc.exe"))) {
    $presets = (git -C $obsSrc show HEAD:CMakePresets.json) -join "`n" | ConvertFrom-Json
    $qt = ($presets.configurePresets | Where-Object name -eq "dependencies").vendor.'obsproject.com/obs-studio'.dependencies.qt6
    $file = "windows-deps-qt6-$($qt.version)-x64.zip"
    $zip = Join-Path $DepsDir $file
    Write-Host "Downloading $file (~260 MB)..."
    curl.exe -L -s -S -o $zip "$($qt.baseUrl)/$($qt.version)/$file"
    if ($LASTEXITCODE -ne 0) { throw "Qt download failed" }
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $qt.hashes.'windows-x64') { throw "Qt package checksum mismatch" }
    New-Item -ItemType Directory -Force $qtDir | Out-Null
    tar -xf $zip -C $qtDir include/QtCore include/QtGui include/QtWidgets include/QtSvg bin/moc.exe licenses
    if ($LASTEXITCODE -ne 0) { throw "Qt extract failed" }
    [IO.File]::Delete($zip)
}

# 4. Import libraries from the installed DLLs
if (-not (Test-Path (Join-Path $DepsDir "importlibs\obs.lib"))) {
    & (Join-Path $PSScriptRoot "make-import-libs.ps1") -ObsDir $ObsDir -OutDir (Join-Path $DepsDir "importlibs")
}

Write-Host "Dependencies ready in $DepsDir" -ForegroundColor Green
