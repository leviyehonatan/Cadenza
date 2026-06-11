# Package Cadenza Workstation into a ready-to-run distributable folder (and
# optionally a .zip). Run from the project root after a Release build:
#
#   cmd /c '"...vcvars64.bat" >nul && cmake --build build-release --target Cadenza style-probe sty-to-cstyle style-scan'
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1 [-Zip]
#
# Output: dist\Cadenza-<version>\  (+ dist\Cadenza-<version>.zip with -Zip)

param(
    [string]$BuildDir = "build-release",
    [string]$OutDir   = "dist",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot   # project root (this script lives in scripts\)

# Version from CMakeLists.txt: project(Cadenza VERSION x.y.z ...)
$cmake = Get-Content (Join-Path $root "CMakeLists.txt") -TotalCount 10 | Out-String
if ($cmake -match 'project\(Cadenza VERSION ([0-9.]+)') { $version = $Matches[1] }
else { $version = "0.0.0" }

$exeDir = Join-Path $root "$BuildDir\Cadenza_artefacts\Release"
$exe    = Join-Path $exeDir "Cadenza Workstation.exe"
if (-not (Test-Path $exe)) {
    Write-Error "Release exe not found at '$exe'. Build the Release tree first."
}

$pkg = Join-Path $root "$OutDir\Cadenza-$version"
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg }
New-Item -ItemType Directory -Force "$pkg\tools" | Out-Null

Write-Host "Packaging Cadenza $version -> $pkg"

# App + runtime DLLs (vcpkg applocal puts libfluidsynth etc. next to the exe).
Copy-Item $exe $pkg
Get-ChildItem $exeDir -Filter *.dll | Copy-Item -Destination $pkg

# Resources: SoundFonts, factory styles/songs, web UI.
Copy-Item -Recurse (Join-Path $root "resources") (Join-Path $pkg "resources")

# CLI tools (style diagnostics / conversion).
foreach ($t in "style-probe.exe", "sty-to-cstyle.exe", "style-scan.exe") {
    $p = Join-Path $root "$BuildDir\$t"
    if (Test-Path $p) { Copy-Item $p "$pkg\tools" }
}

# User-facing quick start.
Copy-Item (Join-Path $root "docs\QUICK_START.md") (Join-Path $pkg "README.md")

$size = [math]::Round((Get-ChildItem $pkg -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host "Package folder ready: $pkg ($size MB)"

if ($Zip) {
    $zipPath = Join-Path $root "$OutDir\Cadenza-$version.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Write-Host "Zipping (this can take a minute, the SoundFonts are large)..."
    Compress-Archive -Path $pkg -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "Zip ready: $zipPath"
}
