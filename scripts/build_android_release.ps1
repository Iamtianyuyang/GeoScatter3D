<#
.SYNOPSIS
    构建并校验正式签名的 Android App Bundle。

.DESCRIPTION
    正式发布路径拒绝缺失或占位签名配置，避免把 debug 签名的 smoke 产物
    误上传到应用商店。密钥文件只从 ui_flutter/android/key.properties 读取，
    不会复制、打印或提交任何密码和私钥材料。
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$AndroidRoot = Join-Path $RepoRoot 'ui_flutter/android'
$FlutterRoot = Join-Path $RepoRoot 'ui_flutter'
$KeyPropertiesFile = Join-Path $AndroidRoot 'key.properties'
$AabPath = Join-Path $FlutterRoot 'build/app/outputs/bundle/release/app-release.aab'

if (-not (Test-Path -LiteralPath $KeyPropertiesFile -PathType Leaf)) {
    throw 'Missing release signing config. Copy ui_flutter/android/key.properties.example to key.properties and fill in the production keystore.'
}

$Properties = @{}
foreach ($Line in Get-Content -LiteralPath $KeyPropertiesFile) {
    $Trimmed = $Line.Trim()
    if ($Trimmed -eq '' -or $Trimmed.StartsWith('#') -or -not $Trimmed.Contains('=')) {
        continue
    }
    $Parts = $Trimmed.Split('=', 2)
    $Properties[$Parts[0].Trim()] = $Parts[1].Trim()
}

foreach ($Name in @('storePassword', 'keyPassword', 'keyAlias', 'storeFile')) {
    if (-not $Properties.ContainsKey($Name) -or [string]::IsNullOrWhiteSpace($Properties[$Name])) {
        throw "Release signing config is missing $Name."
    }
    if ($Properties[$Name] -match 'REPLACE_WITH|CHANGE_ME|TODO') {
        throw "Release signing config $Name still contains a placeholder."
    }
}

$StoreFile = $Properties['storeFile']
if ([IO.Path]::IsPathRooted($StoreFile)) {
    $ResolvedStoreFile = [IO.Path]::GetFullPath($StoreFile)
} else {
    $ResolvedStoreFile = [IO.Path]::GetFullPath((Join-Path $AndroidRoot $StoreFile))
}
if (-not (Test-Path -LiteralPath $ResolvedStoreFile -PathType Leaf)) {
    throw "Production keystore was not found: $ResolvedStoreFile"
}

Push-Location $FlutterRoot
try {
    & flutter build appbundle --release
    if ($LASTEXITCODE -ne 0) {
        throw "Flutter Android AAB build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $AabPath -PathType Leaf)) {
    throw "Build completed but the AAB was not found: $AabPath"
}

# 发布包必须携带所有运行时资源；仅能签名但缺少 ABI/Shader 的 AAB
# 在商店安装后才会失败，因此在签名校验前先检查 ZIP 条目。
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::OpenRead($AabPath)
try {
    $RequiredEntries = @(
        'base/lib/arm64-v8a/libgs3d_ffi.so',
        'base/lib/x86_64/libgs3d_ffi.so',
        'base/lib/arm64-v8a/libgs3d_pipeline_ffi.so',
        'base/lib/x86_64/libgs3d_pipeline_ffi.so',
        'base/assets/flutter_assets/assets/shaders/point.vert.spv',
        'base/assets/flutter_assets/assets/shaders/point.frag.spv',
        'base/assets/flutter_assets/assets/data/sample-points.csv'
    )
    foreach ($EntryName in $RequiredEntries) {
        if ($null -eq $Archive.GetEntry($EntryName)) {
            throw "AAB is missing required runtime entry: $EntryName"
        }
    }
}
finally {
    $Archive.Dispose()
}
Write-Host '==> Android AAB contents verified: native ABI, SPIR-V shaders, sample data' -ForegroundColor Green

$JarsignerPath = $null
$JarsignerCommand = Get-Command jarsigner -ErrorAction SilentlyContinue
if ($JarsignerCommand) {
    $JarsignerPath = $JarsignerCommand.Source
    if ([string]::IsNullOrWhiteSpace($JarsignerPath)) {
        $JarsignerPath = $JarsignerCommand.Path
    }
}
if ([string]::IsNullOrWhiteSpace($JarsignerPath)) {
    $JbrJarsigner = Join-Path ${env:ProgramFiles} 'Android/Android Studio/jbr/bin/jarsigner.exe'
    if (Test-Path -LiteralPath $JbrJarsigner) {
        $JarsignerPath = $JbrJarsigner
    }
}
if ([string]::IsNullOrWhiteSpace($JarsignerPath)) {
    throw 'jarsigner was not found; release signature cannot be verified.'
}

$SignatureOutput = & $JarsignerPath -verify -verbose -certs $AabPath 2>&1
$SignatureExitCode = $LASTEXITCODE
$SignatureText = $SignatureOutput -join [Environment]::NewLine
if ($SignatureExitCode -ne 0) {
    throw "AAB signature verification failed with exit code $SignatureExitCode."
}
if ($SignatureText -match 'CN=Android Debug') {
    throw 'The AAB is still signed with the Android Debug certificate; release delivery is rejected.'
}

Write-Host "==> Android production AAB verified: $AabPath" -ForegroundColor Green
