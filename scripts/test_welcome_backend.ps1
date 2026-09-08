param([ValidateSet('Debug', 'Release')][string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path $PSScriptRoot -Parent
$Runtime = Join-Path $RepoRoot "tmp/flutter-native/$Config"
if (!(Test-Path -LiteralPath (Join-Path $Runtime 'gs3d_ffi.dll'))) { throw 'Build the native runtime first.' }
$TestRoot = Join-Path $RepoRoot ('tmp/welcome-integration-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TestRoot | Out-Null
$Names = @('GS3D_RUNTIME_DIR', 'GS3D_PROJECTS_DIR', 'GS3D_USER_CONFIG_DIR', 'GS3D_RECENT_PROJECTS_PATH', 'GS3D_NATIVE_INTEGRATION')
$Saved = @{}
foreach ($Name in $Names) { $Saved[$Name] = [Environment]::GetEnvironmentVariable($Name, 'Process') }
try {
    $env:GS3D_RUNTIME_DIR = $Runtime
    $env:GS3D_PROJECTS_DIR = Join-Path $TestRoot 'projects'
    $env:GS3D_USER_CONFIG_DIR = Join-Path $TestRoot 'preferences'
    $env:GS3D_RECENT_PROJECTS_PATH = Join-Path $TestRoot 'recent-projects.txt'
    $env:GS3D_NATIVE_INTEGRATION = '1'
    Push-Location (Join-Path $RepoRoot 'ui_flutter')
    try {
        & flutter test test/welcome_backend_integration_test.dart --reporter expanded
        if ($LASTEXITCODE -ne 0) { throw "Welcome backend integration failed: $LASTEXITCODE" }
    } finally { Pop-Location }
} finally {
    foreach ($Name in $Names) { [Environment]::SetEnvironmentVariable($Name, $Saved[$Name], 'Process') }
    $Resolved = [IO.Path]::GetFullPath($TestRoot)
    $Allowed = [IO.Path]::GetFullPath((Join-Path $RepoRoot 'tmp')) + [IO.Path]::DirectorySeparatorChar
    if (!$Resolved.StartsWith($Allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe temporary path' }
    Remove-Item -LiteralPath $Resolved -Recurse -Force
}
