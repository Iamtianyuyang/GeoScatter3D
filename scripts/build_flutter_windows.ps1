<#
.SYNOPSIS
    Build and stage the complete native runtime, then optionally build Flutter.
#>
param(
    [ValidateSet('Debug', 'Profile', 'Release')]
    [string]$Config = 'Release',
    [switch]$NativeOnly,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot 'tmp/build-win'
$FlutterDir = Join-Path $RepoRoot 'ui_flutter'
$NativeConfig = if ($Config -eq 'Profile') { 'Release' } else { $Config }

# VS Code 启动的终端不一定继承 CMake PATH，兼容独立安装和 VS 内置工具。
$Cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($Cmake) { $Cmake = $Cmake.Source } else {
    $Cmake = Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $Cmake)) {
        $Vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $Vswhere) {
            $VsRoot = & $Vswhere -latest -products '*' -property installationPath
            if ($VsRoot) { $Cmake = Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' }
        }
    }
}
if (-not (Test-Path -LiteralPath $Cmake)) { throw 'CMake 3.28+ is required.' }

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$ConfigureArgs = @('-S', $RepoRoot, '-B', $BuildDir, '-DBUILD_TESTING=ON')
if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
    if (-not $VcpkgRoot -and (Test-Path 'C:/vcpkg/scripts/buildsystems/vcpkg.cmake')) {
        $VcpkgRoot = 'C:/vcpkg'
    }
    if ($VcpkgRoot) {
        $ConfigureArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake"
    }
}
Write-Host "==> Building native runtime ($NativeConfig)..."
Invoke-Checked $Cmake $ConfigureArgs
 # Flutter 桌面端通过 gs3d_ffi 直连单进程引擎；不要再构建已移除的
 # GeoScatter3D / GeoScatter3DPreprocess 双进程目标。
Invoke-Checked $Cmake @('--build', $BuildDir, '--config', $NativeConfig, '--target',
    'gs3d_ffi', '--parallel')

# 原生资源只暂存到 tmp；Flutter CMake 的 install 步骤负责部署 Debug/Release。
$StageDir = Join-Path $RepoRoot "tmp/flutter-native/$Config"
$NativeDir = Join-Path $BuildDir "src/$NativeConfig"
$FfiDir = Join-Path $BuildDir "src/ffi/$NativeConfig"
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
Copy-Item -LiteralPath (Join-Path $FfiDir 'gs3d_ffi.dll') -Destination $StageDir -Force
foreach ($RuntimeDir in @($NativeDir, $FfiDir)) {
    Get-ChildItem -LiteralPath $RuntimeDir -Filter '*.dll' | Copy-Item -Destination $StageDir -Force
}
foreach ($Directory in @('assets', 'config', 'examples')) {
    $SourceDir = if ($Directory -eq 'assets') { Join-Path $NativeDir $Directory } else { Join-Path $RepoRoot $Directory }
    $DestinationDir = Join-Path $StageDir $Directory
    New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    Copy-Item (Join-Path $SourceDir '*') $DestinationDir -Recurse -Force
}
Write-Host "==> Native runtime staged at $StageDir"

# Flutter 会随已安装的 VS 工具链切换生成器；保留旧产物并让下一次 F5 重新配置。
$FlutterCmakeDir = Join-Path $FlutterDir 'build/windows/x64'
$FlutterCache = Join-Path $FlutterCmakeDir 'CMakeCache.txt'
$Vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if ((Test-Path -LiteralPath $FlutterCache) -and (Test-Path -LiteralPath $Vswhere)) {
    $CachedGenerator = Select-String -LiteralPath $FlutterCache -Pattern '^CMAKE_GENERATOR:INTERNAL=Visual Studio ([0-9]+) '
    $InstalledVersion = & $Vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    if ($CachedGenerator -and $InstalledVersion -and ([version]$InstalledVersion).Major -ne [int]$CachedGenerator.Matches[0].Groups[1].Value) {
        $SourcePath = [IO.Path]::GetFullPath($FlutterCmakeDir)
        $ArchivePath = [IO.Path]::GetFullPath((Join-Path $RepoRoot ('tmp/flutter-cmake-' + [Guid]::NewGuid().ToString('N'))))
        $AllowedSource = [IO.Path]::GetFullPath((Join-Path $RepoRoot 'ui_flutter/build/windows')) + [IO.Path]::DirectorySeparatorChar
        $AllowedArchive = [IO.Path]::GetFullPath((Join-Path $RepoRoot 'tmp')) + [IO.Path]::DirectorySeparatorChar
        if (!$SourcePath.StartsWith($AllowedSource, [StringComparison]::OrdinalIgnoreCase) -or !$ArchivePath.StartsWith($AllowedArchive, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected Flutter build directory.' }
        Move-Item -LiteralPath $SourcePath -Destination $ArchivePath
        Write-Host "==> Archived obsolete Flutter CMake output to $ArchivePath"
    }
}
if ($NativeOnly) { return }

# 代理拦截 localhost WebSocket 会导致 flutter_tester 和 flutter build 中断。
# 强制绕过代理访问本地地址，不影响其他网络请求。
if (-not $env:NO_PROXY) { $env:NO_PROXY = 'localhost,127.0.0.1' }
elseif ($env:NO_PROXY -notmatch 'localhost') { $env:NO_PROXY = "localhost,127.0.0.1,$env:NO_PROXY" }

Push-Location $FlutterDir
try {
    Invoke-Checked 'flutter' @('build', 'windows', "--$($Config.ToLowerInvariant())")
} finally {
    Pop-Location
}
$RunnerDir = Join-Path $FlutterDir "build/windows/x64/runner/$Config"
foreach ($File in @('ui_flutter.exe', 'gs3d_ffi.dll', 'assets/shaders/point.vert.spv', 'assets/shaders/point.frag.spv')) {
    if (-not (Test-Path -LiteralPath (Join-Path $RunnerDir $File))) {
        throw "Incomplete Flutter runtime: $File"
    }
}
Write-Host "==> Flutter Desktop build succeeded: $RunnerDir/ui_flutter.exe" -ForegroundColor Green
