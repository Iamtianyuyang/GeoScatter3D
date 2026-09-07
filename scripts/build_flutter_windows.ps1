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
    # 运行时目录优先取 exe 实际输出目录（src/<Config>，含 copy_runtime_assets
    # 生成的 assets 与 vcpkg 自动复制的依赖 DLL）；回退到根构建输出
    # ${PROJECT_BINARY_DIR}/<Config>（CMake POST_BUILD 也会复制完整运行时）。
    $NativeReleaseDir = Join-Path $BuildDir "src/$Config"
    if (-not (Test-Path (Join-Path $NativeReleaseDir "GeoScatter3D.exe"))) {
        $NativeReleaseDir = Join-Path $BuildDir $Config
    }
    $NativeExe = Join-Path $NativeReleaseDir "GeoScatter3D.exe"
    if (-not (Test-Path $NativeExe)) {
        throw "Native viewer executable not found: $NativeExe"
    }
    Copy-Item $NativeExe $RunnerReleaseDir -Force

    # glfw3.dll：vcpkg applocal 复制到 exe 同目录；若缺失则从 vcpkg 安装目录回退。
    $GlfwSrc = Join-Path $NativeReleaseDir "glfw3.dll"
    if (-not (Test-Path $GlfwSrc)) {
        $GlfwSrc = Join-Path $env:VCPKG_ROOT "installed/x64-windows/bin/glfw3.dll"
        if (-not $env:VCPKG_ROOT) { $GlfwSrc = "C:/vcpkg/installed/x64-windows/bin/glfw3.dll" }
    }
    if (Test-Path $GlfwSrc) {
        Copy-Item $GlfwSrc $RunnerReleaseDir -Force
    } else {
        Write-Warning "glfw3.dll not found; the runner may fail to load the native viewer."
    }

    # vulkan-1.dll：优先随运行时目录部署；缺失时回退到系统 Vulkan Runtime
    # （%SystemRoot%\System32），仍缺失则跳过（系统已注册 loader 时可正常运行）。
    $VulkanSrc = Join-Path $NativeReleaseDir "vulkan-1.dll"
    if (-not (Test-Path $VulkanSrc)) {
        $VulkanSrc = Join-Path $env:SystemRoot "System32/vulkan-1.dll"
    }
    if (Test-Path $VulkanSrc) {
        Copy-Item $VulkanSrc $RunnerReleaseDir -Force
    } else {
        Write-Warning "vulkan-1.dll not found; relying on system Vulkan runtime."
    }

    $RunnerAssetsDir = Join-Path $RunnerReleaseDir "assets"
    if (-not (Test-Path $RunnerAssetsDir)) {
        New-Item -ItemType Directory -Force -Path $RunnerAssetsDir | Out-Null
    }
    $NativeAssets = Join-Path $NativeReleaseDir "assets"
    if (Test-Path $NativeAssets) {
        Copy-Item (Join-Path $NativeAssets "*") $RunnerAssetsDir -Recurse -Force
    } else {
        Write-Warning "Native assets directory not found: $NativeAssets"
    }
}

Write-Host "==> Flutter Desktop build succeeded! Ready to launch." -ForegroundColor Green
