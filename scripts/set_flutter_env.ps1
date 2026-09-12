<#
.SYNOPSIS
    激活 Flutter 开发所需的环境变量。在 PowerShell 中执行:
    . .\scripts\set_flutter_env.ps1

.NOTES
    代理会拦截 localhost WebSocket，导致 flutter test 和 flutter build 失败。
    此脚本强制绕过代理访问本地地址，同时不影响其他代理设置。
#>

# 防止代理拦截 flutter_tester WebSocket 连接（flutter test / flutter build 依赖）
if (-not $env:NO_PROXY) {
    $env:NO_PROXY = 'localhost,127.0.0.1'
} elseif ($env:NO_PROXY -notmatch 'localhost') {
    $env:NO_PROXY = "localhost,127.0.0.1,$env:NO_PROXY"
}

Write-Host "[GeoScatter3D] Flutter 环境已激活" -ForegroundColor Cyan
Write-Host "  NO_PROXY = $env:NO_PROXY" -ForegroundColor DarkCyan
Write-Host "  现在可以正常运行: flutter test / flutter build" -ForegroundColor DarkCyan
