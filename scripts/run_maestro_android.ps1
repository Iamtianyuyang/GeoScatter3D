param(
    [string]$Device = 'emulator-5554',
    [ValidateSet('all', 'smoke', 'data', 'system')]
    [string]$Suite = 'all',
    [string]$MaestroPath = ''
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Fixture = Join-Path $RepoRoot 'tmp/maestro/geoscatter3d-import-test.csv'
$OutputRoot = Join-Path $RepoRoot 'tmp/maestro'
$RunOutputRelative = Join-Path 'tmp/maestro' ("android-regression-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
$RunOutput = Join-Path $RepoRoot $RunOutputRelative

if ([string]::IsNullOrWhiteSpace($MaestroPath)) {
    $MaestroPath = $env:MAESTRO_CLI_PATH
}
if ([string]::IsNullOrWhiteSpace($MaestroPath)) {
    $MaestroPath = Join-Path $env:USERPROFILE 'tools/maestro/maestro/bin/maestro.bat'
}

if (-not (Test-Path -LiteralPath $MaestroPath)) {
    throw "Maestro CLI not found: $MaestroPath. Set MAESTRO_CLI_PATH or pass -MaestroPath."
}
if (-not (Test-Path -LiteralPath $Fixture)) {
    throw "Android Maestro fixture not found: $Fixture"
}

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    $androidSdkPlatformTools = Join-Path $env:LOCALAPPDATA 'Android/Sdk/platform-tools'
    if (Test-Path $androidSdkPlatformTools) {
        $env:PATH = "$androidSdkPlatformTools;$env:PATH"
    }
}

if (-not $env:JAVA_HOME -or -not (Test-Path $env:JAVA_HOME)) {
    if (Test-Path 'C:\Program Files\Android\openjdk\jdk-21.0.8') {
        $env:JAVA_HOME = 'C:\Program Files\Android\openjdk\jdk-21.0.8'
    } elseif (Test-Path 'C:\Program Files\Android\Android Studio\jbr') {
        $env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
    }
}
if ($env:JAVA_HOME -and -not (Get-Command java -ErrorAction SilentlyContinue)) {
    $env:PATH = "$($env:JAVA_HOME)\bin;$env:PATH"
}

$deviceState = (& adb -s $Device get-state 2>&1).Trim()
if ($LASTEXITCODE -ne 0 -or $deviceState -ne 'device') {
    throw "Android device is not ready: $Device (state: $deviceState)"
}

# 上一轮被中断时，Android DocumentsUI 可能仍占据前台；先关闭外部选择器，
# 避免下一轮 launchApp 实际停留在系统文件面板而误报应用欢迎页断言失败。
& adb -s $Device shell am force-stop 'com.google.android.documentsui'

New-Item -ItemType Directory -Force -Path $RunOutput | Out-Null

# SAF 流程需要在系统 Downloads 中有一个可见 CSV；这是测试前置，不是应用运行时数据。
& adb -s $Device push $Fixture '/sdcard/Download/geoscatter3d-import-test.csv'
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to stage the Maestro CSV fixture on the Android device.'
}

$smokeFlows = @(
    'mobile_native_smoke.yaml',
    'mobile_about_engine.yaml',
    'mobile_workbench_gesture.yaml',
    'mobile_camera_controls.yaml',
    'system_bars.yaml'
)
$dataFlows = @(
    'mobile_document_picker.yaml',
    'mobile_document_directory_picker.yaml',
    'mobile_document_import.yaml',
    'mobile_new_project_import.yaml',
    'mobile_export_saf.yaml',
    'mobile_screenshot_saf_save.yaml'
)
$systemFlows = @('system_bars.yaml')

$flowNames = switch ($Suite) {
    'smoke' { $smokeFlows }
    'data' { $dataFlows }
    'system' { $systemFlows }
    default { $smokeFlows + $dataFlows }
}
$flowPaths = $flowNames | ForEach-Object {
    Join-Path 'tmp/maestro' $_
}

$env:MAESTRO_CLI_NO_ANALYTICS = '1'
$env:ANDROID_USER_HOME = Join-Path $env:USERPROFILE '.android'
$env:JAVA_OPTS = "-Duser.home=$($env:USERPROFILE)"

$maestroExitCode = 1
$previousLocation = Get-Location
try {
    Set-Location $RepoRoot
    try {
        & $MaestroPath --device $Device test `
            --no-ansi `
            --reinstall-driver=false `
            --test-output-dir $RunOutputRelative `
            $flowPaths
        $maestroExitCode = $LASTEXITCODE
    }
    finally {
        # fixture 只属于本次测试，CLI 失败时也不能留在用户设备 Downloads。
        & adb -s $Device shell rm -f '/sdcard/Download/geoscatter3d-import-test.csv'
        # SAF 是外部 Activity；无论 flow 成功、失败还是被中止，都关闭残留选择器。
        & adb -s $Device shell am force-stop 'com.google.android.documentsui'
    }
}
finally {
    Set-Location $previousLocation
}
if ($maestroExitCode -ne 0) {
    throw "Maestro Android regression failed for suite '$Suite'. Output: $RunOutput"
}

Write-Host "Maestro Android regression passed: $RunOutput" -ForegroundColor Green
