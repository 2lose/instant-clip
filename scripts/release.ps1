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

# Releases must carry every embedded value: without the token they can't update on PCs
# without gh, and without the relay the Feedback button disappears.
$required = @{
    "update-token.txt"       = "INSTANT_CLIP_UPDATE_TOKEN"
    "feedback-relay-url.txt" = "INSTANT_CLIP_FEEDBACK_URL"
    "feedback-relay-key.txt" = "INSTANT_CLIP_FEEDBACK_KEY"
}
foreach ($file in $required.Keys) {
    if (-not [Environment]::GetEnvironmentVariable($required[$file]) -and -not (Test-Path (Join-Path $root "secrets\$file"))) {
        throw "Missing secrets\$file (see README)."
    }
}

if (git status --porcelain 2>$null) {
    Write-Host "Uncommitted changes will be included in this release commit:" -ForegroundColor Yellow
    git status --short
}

# Version lives in buildspec.json (the top-level "version", right after "displayName").
$buildspec = Join-Path $root "buildspec.json"
$text = [IO.File]::ReadAllText($buildspec)
$text = [regex]::Replace($text, '("displayName":\s*"[^"]*",\s*"version":\s*")[^"]+', "`${1}$Version")
[IO.File]::WriteAllText($buildspec, $text)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# Encrypt this release's values into the build, then build with the standard preset.
& (Join-Path $PSScriptRoot "embed-secrets.ps1") -Out (Join-Path $root "build_x64\gen\embedded-secrets.inc")
& $cmake --preset windows-x64
if ($LASTEXITCODE -ne 0) { throw "Configure failed" }
& $cmake --build --preset windows-x64
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Package: instant-clip/bin/64bit/instant-clip.dll + instant-clip/data/... (no .pdb)
$out = Join-Path $root "build_x64\release"
$stageRoot = Join-Path $out "stage"
$stage = Join-Path $stageRoot "instant-clip"
if (Test-Path $stageRoot) { [IO.Directory]::Delete($stageRoot, $true) }
& $cmake --install (Join-Path $root "build_x64") --config RelWithDebInfo --prefix $stageRoot
if ($LASTEXITCODE -ne 0) { throw "Install to staging failed" }
Get-ChildItem $stage -Recurse -Filter *.pdb | ForEach-Object { [IO.File]::Delete($_.FullName) }

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