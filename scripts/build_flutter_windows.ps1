<#
.SYNOPSIS
    Builds the C++ FFI dynamic library and the Flutter Windows Desktop application.
#>

param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

Write-Host "==> [1/4] Terminating any running application processes..." -ForegroundColor Cyan
Stop-Process -Name ui_flutter, GeoScatter3D -Force -ErrorAction SilentlyContinue

Write-Host "==> [2/4] Building C++ FFI library (gs3d_ffi)..." -ForegroundColor Cyan
$BuildDir = Join-Path $RepoRoot "tmp/build-win"
if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    cmake -B $BuildDir -S $RepoRoot -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DBUILD_TESTING=ON
}
cmake --build $BuildDir --config $Config --target gs3d_ffi GeoScatter3D --parallel

$SourceDll = Join-Path $BuildDir "src/ffi/$Config/gs3d_ffi.dll"
$FlutterDir = Join-Path $RepoRoot "ui_flutter"
$FlutterDll = Join-Path $FlutterDir "gs3d_ffi.dll"
Copy-Item $SourceDll $FlutterDll -Force

$FlutterFontsDir = Join-Path $FlutterDir "assets/fonts"
if (-not (Test-Path $FlutterFontsDir)) {
    New-Item -ItemType Directory -Force -Path $FlutterFontsDir | Out-Null
}
Copy-Item (Join-Path $RepoRoot "assets/fonts/*") $FlutterFontsDir -Force

$env:NO_PROXY = "localhost,127.0.0.1"
$env:HTTP_PROXY = ""
$env:HTTPS_PROXY = ""
$env:ALL_PROXY = ""
Push-Location $FlutterDir
try {
    flutter build windows
} finally {
    Pop-Location
}

Write-Host "==> [4/4] Deploying FFI dynamic library to release runner..." -ForegroundColor Cyan
$RunnerReleaseDir = Join-Path $FlutterDir "build/windows/x64/runner/$Config"
if (Test-Path $RunnerReleaseDir) {
    Copy-Item $FlutterDll (Join-Path $RunnerReleaseDir "gs3d_ffi.dll") -Force

    # Flutter 的原生引擎桥接会在本机回环地址启动无窗口 ViewerApp，必须随前端
    # 一起部署其可执行文件、运行时 DLL 与 shader/font 资源，不能退回到 Dart 预览。
    $NativeReleaseDir = Join-Path $BuildDir $Config
    Copy-Item (Join-Path $NativeReleaseDir "GeoScatter3D.exe") $RunnerReleaseDir -Force
    Copy-Item (Join-Path $NativeReleaseDir "glfw3.dll") $RunnerReleaseDir -Force
    Copy-Item (Join-Path $NativeReleaseDir "vulkan-1.dll") $RunnerReleaseDir -Force
    $RunnerAssetsDir = Join-Path $RunnerReleaseDir "assets"
    if (-not (Test-Path $RunnerAssetsDir)) {
        New-Item -ItemType Directory -Force -Path $RunnerAssetsDir | Out-Null
    }
    Copy-Item (Join-Path $NativeReleaseDir "assets/*") $RunnerAssetsDir -Recurse -Force
}

Write-Host "==> Flutter Desktop build succeeded! Ready to launch." -ForegroundColor Green
