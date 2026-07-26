#!/usr/bin/env pwsh
# One-click Windows build + package.
#
# Installs vcpkg dependencies, builds Release, runs the test suite, then
# produces the self-contained distribution folder (dist/) and the CPack ZIP.
#
# Usage:
#   ./scripts/package-windows.ps1                # full pipeline
#   ./scripts/package-windows.ps1 -SkipTests     # skip ctest
#   ./scripts/package-windows.ps1 -BuildDir out  # custom build directory

[CmdletBinding()]
param(
    [string]$BuildDir = "build-win",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

# Native commands do not throw on failure; check exit codes explicitly.
function Exec {
    param([scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $Command"
    }
}

# FindPython3 needs a real interpreter; the store alias in WindowsApps is a
# stub that must be skipped. Checked in order: PATH, the py launcher, then
# well-known per-user conda installs.
function Find-Python3 {
    foreach ($name in "python", "python3") {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source -notmatch "WindowsApps") {
            $major = & $cmd.Source -c "import sys; print(sys.version_info[0])" 2>$null
            if ($major -eq "3") { return $cmd.Source }
        }
    }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        $exe = & $py.Source -3 -c "import sys; print(sys.executable)" 2>$null
        if ($exe -and (Test-Path $exe)) { return $exe }
    }
    foreach ($candidate in "$env:USERPROFILE\miniconda3\python.exe",
                           "$env:USERPROFILE\anaconda3\python.exe") {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$Python = Find-Python3
if (-not $Python) {
    throw "Python 3 not found (required by the include-dependency checks). " +
        "Install it from https://www.python.org and re-run."
}
Write-Host "==> Python 3: $Python"

# --- Locate vcpkg -----------------------------------------------------------
$VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $VcpkgRoot -and (Test-Path "C:\vcpkg\vcpkg.exe")) {
    $VcpkgRoot = "C:\vcpkg"
}
if (-not $VcpkgRoot) {
    $VcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($VcpkgCmd) { $VcpkgRoot = Split-Path -Parent $VcpkgCmd.Source }
}
if (-not $VcpkgRoot) {
    throw ("vcpkg not found. Install it (https://vcpkg.io) and set " +
        "VCPKG_INSTALLATION_ROOT, or place it at C:\vcpkg.")
}
Write-Host "==> vcpkg: $VcpkgRoot"

# --- Dependencies (idempotent: fast when already installed) -----------------
Write-Host "==> Installing vcpkg dependencies"
Exec { & "$VcpkgRoot\vcpkg.exe" install glfw3:x64-windows `
        vulkan-headers:x64-windows vulkan-loader:x64-windows `
        "glslang[tools]:x64-windows" }

Write-Host "==> Initializing git submodules"
Exec { git submodule update --init --recursive }

# --- Configure + build ------------------------------------------------------
# BUILD_TESTING is passed explicitly so a stale cache with tests disabled
# can never silently skip the test targets.
Write-Host "==> Configuring ($BuildDir)"
Exec { cmake -B $BuildDir -S . `
        -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
        -DPython3_EXECUTABLE="$Python" `
        -DBUILD_TESTING=ON }

Write-Host "==> Building Release"
Exec { cmake --build $BuildDir --config Release --parallel }

if (-not $SkipTests) {
    Write-Host "==> Running tests"
    Exec { ctest --test-dir $BuildDir -C Release --output-on-failure }
}

# --- Install + package ------------------------------------------------------
Write-Host "==> Installing to dist/"
if (Test-Path dist) { Remove-Item -Recurse -Force dist }
Exec { cmake --install $BuildDir --config Release --prefix dist }

Write-Host "==> Creating package"
Push-Location $BuildDir
try {
    Exec { cpack -C Release }
} finally {
    Pop-Location
}

$Package = Get-ChildItem "$BuildDir\GeoScatter3D-*.zip" |
    Sort-Object LastWriteTime | Select-Object -Last 1
Write-Host ""
Write-Host "Done."
Write-Host "  Folder:  $RepoRoot\dist\"
Write-Host "  Archive: $($Package.FullName)"
