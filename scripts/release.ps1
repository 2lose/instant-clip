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


if (git status --porcelain 2>$null) {
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
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
# Entries written one by one so they use '/' separators (Windows PowerShell's helpers write '\').
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
$base = Split-Path $stage -Parent
foreach ($file in Get-ChildItem $stage -Recurse -File) {
    $entry = $file.FullName.Substring($base.Length + 1).Replace('\', '/')
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal)
}
$archive.Dispose()
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText("$zip.sha256", "$hash  $name`n")

# Native tools write progress to stderr; judge them by exit code instead.
function Run([scriptblock]$cmd) {
    $ErrorActionPreference = "Continue"
    & $cmd 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "Failed: $cmd" }
}

Run { git add -A }
if (git status --porcelain) { Run { git commit -m "Release v$Version" } }
if (-not (git tag -l "v$Version")) { Run { git tag "v$Version" } }
Run { git push origin HEAD --tags }

if (-not $Notes) { $Notes = "Instant Clip $Version" }
Run { gh release create "v$Version" $zip "$zip.sha256" --repo $Repo --title "v$Version" --notes $Notes }
Write-Host "Published v$Version. Running plugins will offer it within 6 hours, or on 'Check now'." -ForegroundColor Green