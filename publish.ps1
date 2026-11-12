<#
.SYNOPSIS
    把 AntiBang 发布到你的 GitHub 仓库（用你自己的登录态，不经过任何第三方）

.DESCRIPTION
    1. 先在浏览器里建一个空的仓库（不要勾选 README / .gitignore / LICENSE）
    2. 在服务端或任意目录运行本脚本，按提示填仓库地址
    3. 首次 push 会弹出 GitHub 登录窗口，用你 Edge 里那个账号登录即可

.EXAMPLE
    .\publish.ps1
    .\publish.ps1 -RepoUrl https://github.com/your-name/AntiBang.git
#>
[CmdletBinding()]
param(
    [string]$RepoUrl,
    [string]$UserName,
    [string]$UserEmail
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Ask([string]$prompt, [string]$default = '') {
    if ($default) {
        $v = Read-Host "$prompt [$default]"
        if ([string]::IsNullOrWhiteSpace($v)) { return $default }
        return $v.Trim()
    }
    do { $v = Read-Host $prompt } while ([string]::IsNullOrWhiteSpace($v))
    return $v.Trim()
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "[x] 没找到 git，先装：https://git-scm.com/download/win" -ForegroundColor Red
    exit 1
}

if (-not $RepoUrl) {
    Write-Host "先在浏览器打开 https://github.com/new 建一个空仓库（不要勾选任何初始化选项）" -ForegroundColor Cyan
    Write-Host "仓库名建议：AntiBang" -ForegroundColor DarkGray
    $RepoUrl = Ask "粘贴仓库地址（形如 https://github.com/用户名/AntiBang.git）"
}

# 规范化地址
$RepoUrl = $RepoUrl.Trim()
if ($RepoUrl -notmatch '^https://' -and $RepoUrl -notmatch '^git@') {
    $RepoUrl = "https://github.com/$($RepoUrl.TrimEnd('/')).git"
}
if ($RepoUrl -notmatch '\.git$') { $RepoUrl += '.git' }

if (-not $UserName) { $UserName = Ask "git 提交用的名字（一般填 GitHub 用户名）" }
if (-not $UserEmail) { $UserEmail = Ask "git 提交用的邮箱（GitHub 账号邮箱）" }

Write-Host "`n==> 初始化仓库" -ForegroundColor Cyan
if (-not (Test-Path '.git')) { git init -b main | Out-Null }

git config user.name  $UserName
git config user.email $UserEmail

git add -A

Write-Host "`n==> 待提交文件" -ForegroundColor Cyan
git status --short

$msg = Ask "提交信息" "feat: AntiBang —— BDS 防熊插件（封 XUID/设备码/IP）"
git commit -m $msg | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host "[!] 没有新改动或提交失败，继续尝试推送" -ForegroundColor Yellow }

Write-Host "`n==> 设置远程并推送" -ForegroundColor Cyan
$existing = git remote get-url origin 2>$null
if ($existing) { git remote set-url origin $RepoUrl } else { git remote add origin $RepoUrl }

git push -u origin main

if ($LASTEXITCODE -eq 0) {
    $web = $RepoUrl -replace '\.git$', ''
    Write-Host "`n[OK] 已推送：$web" -ForegroundColor Green
    Write-Host "接下来去仓库页面补两样：" -ForegroundColor Green
    Write-Host "  - About 简介：BDS 防熊插件：封 XUID / 设备码 / IP，并记录全部进服玩家"
    Write-Host "  - Topics：minecraft bedrock levilamina bds anti-cheat ban-plugin"
} else {
    Write-Host "`n[x] 推送失败。常见原因：" -ForegroundColor Red
    Write-Host "  1. 仓库名不对或仓库里已有内容（新建时勾了 README）"
    Write-Host "  2. 没有登录：再执行一次，弹出的登录窗口里登录 GitHub"
    Write-Host "  3. 账号没权限：确认仓库是你自己名下的"
}
