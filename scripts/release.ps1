# Publishes a new version: bumps the version, builds, packages the zip + .sha256,
# commits, tags and creates the GitHub release the plugin's updater looks for.
#   .\scripts\release.ps1 1.2.0 "What changed"
param(
    [Parameter(Mandatory)][string]$Version,
    [string]$Notes = "",
    [string]$Repo = "2lose/instant-clip"
)

$ErrorActionPreference = "Stop"
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must look like 1.2.0" }
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $root

if (git status --porcelain -- . ':!CMakeLists.txt') {
    Write-Host "Uncommitted changes will be included in this release commit:" -ForegroundColor Yellow
    git status --short
}

# Version lives in CMakeLists.txt (project(... VERSION x.y.z ...)).
$cmakeLists = Join-Path $root "CMakeLists.txt"
$text = [IO.File]::ReadAllText($cmakeLists)
$text = [regex]::Replace($text, 'project\(instant-clip VERSION \d+\.\d+\.\d+', "project(instant-clip VERSION $Version")
[IO.File]::WriteAllText($cmakeLists, $text)

& (Join-Path $PSScriptRoot "build.ps1") -NoInstall

# Package: instant-clip/bin/64bit/instant-clip.dll + instant-clip/data/...
$out = Join-Path $root "build\release"
$stage = Join-Path $out "stage\instant-clip"
Remove-Item -Recurse -Force (Join-Path $out "stage") -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $stage "bin\64bit") | Out-Null
Copy-Item (Join-Path $root "build\RelWithDebInfo\instant-clip.dll") (Join-Path $stage "bin\64bit")
Copy-Item -Recurse (Join-Path $root "data") $stage

$name = "instant-clip-$Version-windows-x64.zip"
$zip = Join-Path $out $name
Remove-Item $zip, "$zip.sha256" -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText("$zip.sha256", "$hash  $name`n")

git add -A
if (git status --porcelain) { git commit -m "Release v$Version" }
git tag "v$Version"
git push origin HEAD --tags

if (-not $Notes) { $Notes = "Instant Clip $Version" }
gh release create "v$Version" $zip "$zip.sha256" --repo $Repo --title "v$Version" --notes $Notes
Write-Host "Published v$Version. Running plugins will offer it within 6 hours, or on 'Check now'." -ForegroundColor Green
