<#
.SYNOPSIS
    使用 Android NDK 交叉编译 libgs3d_ffi.so (arm64-v8a)，
    并自动部署到 ui_flutter/android/app/src/main/jniLibs/arm64-v8a/

.PARAMETER NdkRoot
    NDK 根目录，默认自动探测 $LOCALAPPDATA/Android/Sdk/ndk/<最新版本>

.PARAMETER AndroidApiLevel
    Android API 级别，默认 29 (Android 10，Tab S7 最低受支持版本)

.EXAMPLE
    .\scripts\build_android_arm64.ps1
    .\scripts\build_android_arm64.ps1 -NdkRoot "C:\Android\Sdk\ndk\28.2.13676358"
#>
param(
    [string]$NdkRoot = '',
    [int]$AndroidApiLevel = 29
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir  = Join-Path $RepoRoot 'tmp/build-android-arm64'
$JniLibDir = Join-Path $RepoRoot 'ui_flutter/android/app/src/main/jniLibs/arm64-v8a'

# ── NDK 探测 ────────────────────────────────────────────────────────────────
if (-not $NdkRoot) {
    $NdkRoot = $env:ANDROID_NDK_HOME
}
if (-not $NdkRoot -or -not (Test-Path $NdkRoot)) {
    $SdkRoot = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { $env:ANDROID_SDK_ROOT }
    if (-not $SdkRoot) { $SdkRoot = "$env:LOCALAPPDATA\Android\Sdk" }
    $NdkDir = Join-Path $SdkRoot 'ndk'
    if (Test-Path $NdkDir) {
        $NdkRoot = (Get-ChildItem $NdkDir | Sort-Object Name -Descending | Select-Object -First 1).FullName
    }
}
if (-not $NdkRoot -or -not (Test-Path $NdkRoot)) {
    throw "找不到 Android NDK，请设置 ANDROID_NDK_HOME 或通过 -NdkRoot 参数指定。"
}
$Toolchain = Join-Path $NdkRoot 'build/cmake/android.toolchain.cmake'
if (-not (Test-Path $Toolchain)) {
    throw "NDK toolchain 文件不存在: $Toolchain"
}
Write-Host "==> NDK: $NdkRoot" -ForegroundColor Cyan
Write-Host "==> API Level: $AndroidApiLevel" -ForegroundColor Cyan

# ── CMake 探测 ──────────────────────────────────────────────────────────────
$Cmake = (Get-Command cmake -ErrorAction SilentlyContinue)?.Source
if (-not $Cmake) { $Cmake = Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe' }
if (-not (Test-Path -LiteralPath $Cmake)) { throw 'CMake 3.28+ is required.' }

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed (exit $LASTEXITCODE)" }
}

# ── 配置 ────────────────────────────────────────────────────────────────────
Write-Host "==> Configuring arm64-v8a build..." -ForegroundColor Cyan
Invoke-Checked $Cmake @(
    '-S', $RepoRoot,
    '-B', $BuildDir,
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
    '-DANDROID_ABI=arm64-v8a',
    "-DANDROID_PLATFORM=android-$AndroidApiLevel",
    '-DANDROID_STL=c++_shared',
    '-DCMAKE_BUILD_TYPE=Release',
    '-DBUILD_TESTING=OFF',
    # nlohmann/json 头文件在 third_party 下，Android 不走 vcpkg
    "-DNLOHMANN_JSON_INCLUDE_DIR=$RepoRoot/third_party/nlohmann/include"
)

# ── 编译 ────────────────────────────────────────────────────────────────────
Write-Host "==> Building libgs3d_ffi.so (arm64-v8a)..." -ForegroundColor Cyan
Invoke-Checked $Cmake @('--build', $BuildDir, '--target', 'gs3d_ffi', '--parallel')

# ── 探测产物 ────────────────────────────────────────────────────────────────
$SoFile = Get-ChildItem -Recurse $BuildDir -Filter 'libgs3d_ffi.so' |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $SoFile) { throw "编译产物 libgs3d_ffi.so 未找到" }
Write-Host "==> Built: $($SoFile.FullName)" -ForegroundColor Green

# ── 部署到 jniLibs ──────────────────────────────────────────────────────────
New-Item -ItemType Directory -Force -Path $JniLibDir | Out-Null
Copy-Item -LiteralPath $SoFile.FullName -Destination (Join-Path $JniLibDir 'libgs3d_ffi.so') -Force
Write-Host "==> Deployed to $JniLibDir" -ForegroundColor Green
Write-Host "==> arm64-v8a cross-compilation complete." -ForegroundColor Green
