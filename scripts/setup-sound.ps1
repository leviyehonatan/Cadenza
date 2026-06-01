# setup-sound.ps1
# Installs vcpkg + FluidSynth, reconfigures Cadenza's CMake build to use them,
# rebuilds, and tells you where to put a SoundFont so you can hear audio.
#
# Run from PowerShell (no admin required if vcpkg + the project live in your user folder).
# This script is idempotent — re-running it just confirms the state.

$ErrorActionPreference = "Stop"

# ---------- locations ----------
$ProjectRoot = "C:\Users\suko5\Desktop\arranger workstation inspired by giglad"
$VcpkgRoot   = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "$env:USERPROFILE\vcpkg" }
$SfDir       = Join-Path $ProjectRoot "resources\sf2"

Write-Host ""
Write-Host "=== Cadenza sound setup ===" -ForegroundColor Cyan
Write-Host "Project root: $ProjectRoot"
Write-Host "vcpkg root:   $VcpkgRoot"
Write-Host ""

# ---------- 1. git available? ----------
$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    Write-Host "Git is required but not on PATH." -ForegroundColor Red
    Write-Host "Install Git for Windows: https://git-scm.com/download/win"
    Write-Host "Then re-run this script."
    exit 1
}

# ---------- 2. vcpkg present? ----------
if (-not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Host "[1/5] Cloning vcpkg into $VcpkgRoot ..."
    if (-not (Test-Path $VcpkgRoot)) {
        git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    } else {
        Write-Host "    $VcpkgRoot exists but no vcpkg.exe yet; pulling latest."
        Push-Location $VcpkgRoot
        git pull
        Pop-Location
    }

    Write-Host "[2/5] Bootstrapping vcpkg ..."
    Push-Location $VcpkgRoot
    .\bootstrap-vcpkg.bat -disableMetrics
    Pop-Location
} else {
    Write-Host "[1-2/5] vcpkg already installed at $VcpkgRoot"
}

# ---------- 3. install fluidsynth ----------
Write-Host ""
Write-Host "[3/5] Installing fluidsynth (this can take 10-20 min the first time) ..."
& "$VcpkgRoot\vcpkg.exe" install fluidsynth:x64-windows
if ($LASTEXITCODE -ne 0) {
    Write-Host "vcpkg install fluidsynth failed." -ForegroundColor Red
    exit 1
}

# ---------- 4. reconfigure Cadenza's CMake to use the vcpkg toolchain ----------
Write-Host ""
Write-Host "[4/5] Reconfiguring Cadenza CMake with vcpkg toolchain ..."
Set-Location $ProjectRoot

$toolchain = (Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake").Replace("/", "\")
$vcvars    = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if (-not (Test-Path $vcvars)) {
    Write-Host "Cannot find vcvars64.bat at:" -ForegroundColor Red
    Write-Host "  $vcvars"
    Write-Host "Adjust the path in setup-sound.ps1 (line 'vcvars=') for your VS install."
    exit 1
}

# Wipe the previous CMake cache so the toolchain is picked up cleanly.
if (Test-Path "$ProjectRoot\build-msvc\CMakeCache.txt") {
    Remove-Item "$ProjectRoot\build-msvc\CMakeCache.txt"
}

# Run cmake inside a vcvars-initialised cmd shell so cl.exe + INCLUDE/LIB are visible.
$cmdLine = "`"$vcvars`" >nul && cmake -S . -B build-msvc -G Ninja `"-DCMAKE_TOOLCHAIN_FILE=$toolchain`" `"-DVCPKG_TARGET_TRIPLET=x64-windows`""
& cmd /c $cmdLine
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure failed." -ForegroundColor Red
    exit 1
}

# ---------- 5. rebuild ----------
Write-Host ""
Write-Host "[5/5] Rebuilding Cadenza with real FluidSynth ..."
& cmd /c ".\build.bat"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed." -ForegroundColor Red
    exit 1
}

# ---------- final instructions ----------
Write-Host ""
Write-Host "=== DONE ===" -ForegroundColor Green
Write-Host ""
Write-Host "FluidSynth is installed and Cadenza is built against it."
Write-Host ""

$sf2Files = Get-ChildItem -Path $SfDir -Filter *.sf2 -ErrorAction SilentlyContinue
if (-not $sf2Files) {
    Write-Host "You still need a SoundFont. Easiest free option:" -ForegroundColor Yellow
    Write-Host "  1. Open https://schristiancollins.com/generaluser.php"
    Write-Host "  2. Download 'GeneralUser GS' (about 30 MB)"
    Write-Host "  3. Extract the .sf2 file from the zip"
    Write-Host "  4. Drop it into:  ${SfDir}"
    Write-Host ""
    Write-Host "Then launch Cadenza:"
} else {
    Write-Host "Found SoundFont(s) in ${SfDir}:"
    $sf2Files | ForEach-Object { Write-Host "  - $($_.Name)" }
    Write-Host ""
    Write-Host "Launch Cadenza:"
}
Write-Host "  & `"$ProjectRoot\build-msvc\Cadenza_artefacts\Debug\Cadenza Workstation.exe`""
Write-Host ""
