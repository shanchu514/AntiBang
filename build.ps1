<#
.SYNOPSIS
    一键编译 AntiBang 插件（LeviLamina 服务端）

.EXAMPLE
    .\build.ps1                       # 默认按 LL 26.20.* 编译（BDS 1.26.20）
    .\build.ps1 -LLVersion 1.4.4      # 按 LL 1.4.x 编译（BDS 1.21.93）
    .\build.ps1 -LLVersion "26.20.*" -Mode debug

.NOTES
    需要：Visual Studio 2022（勾选「使用 C++ 的桌面开发」）+ xmake。
    没装 xmake 时脚本会提示安装命令。
#>
[CmdletBinding()]
param(
    [string]$LLVersion = '26.20.*',
    [ValidateSet('release', 'debug')]
    [string]$Mode = 'release'
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

# ---- 本机工具链环境 ----
# xmake 全局目录放 D 盘，避免往 C 盘塞 LL SDK；临时目录必须是纯 ASCII 路径，
# 否则 clang 编译 .rc 时会报 "No mapping for the Unicode character..."。
if (-not $env:XMAKE_GLOBALDIR) { $env:XMAKE_GLOBALDIR = 'D:\dev\xmake-global' }
$tempDir = 'D:\dev\temp'
if (Test-Path -LiteralPath (Split-Path $tempDir -Parent)) {
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
    $env:TMP = $tempDir
    $env:TEMP = $tempDir
}
# GitHub 直连不通，git 操作走 gh-proxy 镜像（仅本进程生效）
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'url.https://gh-proxy.com/https://github.com/.insteadOf'
$env:GIT_CONFIG_VALUE_0 = 'https://github.com/'

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Warn2($text) { Write-Host "[!] $text" -ForegroundColor Yellow }
function Die($text) { Write-Host "[x] $text" -ForegroundColor Red; exit 1 }

# ---------- 1. xmake ----------
Step "检查 xmake"
$xmake = $null
$cmd = Get-Command xmake -ErrorAction SilentlyContinue
if ($cmd) { $xmake = $cmd.Source }
if (-not $xmake) {
    $guess = Join-Path $env:LOCALAPPDATA 'xmake\xmake.exe'
    if (Test-Path -LiteralPath $guess) { $xmake = $guess }
}
if (-not $xmake -and (Test-Path -LiteralPath 'D:\dev\xmake\xmake.exe')) {
    $xmake = 'D:\dev\xmake\xmake.exe'
}
if (-not $xmake) {
    Warn2 "没找到 xmake，先装一个（任选其一）："
    Write-Host "    winget install Xmake.Xmake"
    Write-Host "    或：Invoke-Expression (Invoke-WebRequest https://xmake.io/psget.text -UseBasicParsing).Content"
    Write-Host "    装完关掉这个窗口重开，再双击一次。"
    exit 1
}
Write-Host "    $xmake"

# ---------- 2. Visual Studio ----------
Step "检查 Visual Studio 的 C++ 工具链"
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $vsVersion = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationVersion 2>$null
    if ($vsVersion) {
        Write-Host "    VS $vsVersion（含 C++ 生成工具）"
    } else {
        Warn2 "装了 Visual Studio，但没找到「使用 C++ 的桌面开发」工作负载。"
        Warn2 "打开 Visual Studio Installer -> 修改 -> 勾选「使用 C++ 的桌面开发」再试。"
    }
} else {
    Warn2 "没找到 vswhere.exe，说明这台机器可能没装 Visual Studio。"
    Warn2 "编译 BDS 插件必须要它：https://visualstudio.microsoft.com/vs/community/"
    Warn2 "安装时务必勾选「使用 C++ 的桌面开发」。"
}

# ---------- 3. 编译 ----------
Step "更新 xmake 仓库"
& $xmake repo -u
if ($LASTEXITCODE -ne 0) { Warn2 "xmake 仓库更新失败（GitHub 连接问题），继续用本地缓存。" }

Step "配置：LL $LLVersion / $Mode / windows x64"
& $xmake f -a x64 -m $Mode -p windows --target_type=server "--ll_version=$LLVersion" -y
if ($LASTEXITCODE -ne 0) {
    Die "配置失败。最常见原因：xmake 仓库里没有 levilamina $LLVersion。`n    换成具体小版本再试，例如：.\build.ps1 -LLVersion 1.4.4"
}

Step "编译（第一次会下载 LL SDK，比较慢，别关窗口）"
& $xmake -y
if ($LASTEXITCODE -ne 0) {
    Die "编译失败。把上面红色报错整段发给我，我按你实际的 LL 版本改代码。"
}

# ---------- 4. 收集产物 ----------
Step "收集产物"
$dll = $null
$binDir = Join-Path $PSScriptRoot 'bin'
if (Test-Path -LiteralPath $binDir) {
    $dll = Get-ChildItem -LiteralPath $binDir -Recurse -Filter 'AntiBang.dll' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
if (-not $dll) {
    $dll = Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -Filter 'AntiBang.dll' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notlike '*\out\*' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
if (-not $dll) { Die "编译过程看起来成功了，但没找到 AntiBang.dll。把上面日志发我。" }

$outName = 'AntiBang-' + ($LLVersion -replace '\*', 'x')
$outDir = Join-Path (Join-Path $PSScriptRoot 'out') $outName
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $outDir 'AntiBang.dll') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Destination (Join-Path $outDir 'manifest.json') -Force

Write-Host ""
Write-Host "编译完成。" -ForegroundColor Green
Write-Host "  产物目录: $outDir"
Write-Host ""
Write-Host "下一步：" -ForegroundColor Green
Write-Host "  1. 停服"
Write-Host "  2. 把上面这个文件夹整个复制到  <服务端根目录>\plugins\AntiBang\"
Write-Host "     （即 plugins\AntiBang\AntiBang.dll 和 plugins\AntiBang\manifest.json）"
Write-Host "  3. 启动服务端，控制台出现 [AntiBang] 已启用 即可"
Write-Host ""
Write-Host "装好后常用命令：" -ForegroundColor Green
Write-Host "  /abcheck 玩家名            看他的 XUID、设备码、IP"
Write-Host "  /abban 玩家名 原因         把 XUID + 设备码 + IP 一起封掉"
Write-Host "  /abunban 设备码或IP或名字   解封"
Write-Host "  /ablist                    看封禁列表"
Write-Host ""
Write-Host "如果这是旧版本服务端（BDS 1.21.93），记得用 -LLVersion 1.4.4 再编一份。" -ForegroundColor Yellow
