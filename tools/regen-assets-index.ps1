# ============================================================================
#  重新生成 assets/.assets.index
#
#  ── 这个文件是什么 ─────────────────────────────────────────────────────────
#
#  Momentics 为 assets 目录生成的**索引文件**。格式很简单（纯文本）：
#
#      第 1 行：格式版本（1）
#      第 2 行：条目数量
#      之后每行：一个相对 assets/ 的路径
#
#  实测内容形如：
#      1
#      16
#      BaItem.qml
#      ForumDetailPage.qml
#      icons/ic_browser.png
#      ...
#
#  ── 为什么需要手动重生成 ───────────────────────────────────────────────────
#
#  这个文件**不会**被 make 重新生成 —— 只有 IDE 里改动 assets 目录时才会更新。
#  于是出现这种情况：新加了图标文件、也在 config.pri 里登记了，
#  但 .assets.index 还是旧的、不含新文件。
#
#  assets 本身是由 bar-descriptor.xml 的
#      <asset path="assets">assets</asset>
#  整个目录打包的，所以理论上索引旧一点也能用；但为了让部署结果和索引一致
#  （也避免某些流程真的去查索引），这里提供一个显式的重生成步骤。
#
#  ── 用法 ───────────────────────────────────────────────────────────────────
#      powershell -NoProfile -ExecutionPolicy Bypass -File tools\regen-assets-index.ps1
#
#  需要 UTF-8 BOM：本文件含中文注释。
# ============================================================================

[CmdletBinding()]
param(
    # assets 目录（默认取脚本上一级的 assets）
    #
    # 注意：**不能**在 param 默认值里用 $PSScriptRoot —— 用 -File 调用时它是空的，
    # 会报 "Cannot bind argument to parameter 'Path' because it is an empty string"。
    # 用 $MyInvocation.MyCommand.Path 取脚本自身路径才可靠。
    [string] $AssetsDir = '',
    # 只报告不写入
    [switch] $WhatIfOnly
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrEmpty($AssetsDir)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    if ([string]::IsNullOrEmpty($scriptDir)) { $scriptDir = (Get-Location).Path }
    $AssetsDir = Join-Path (Split-Path $scriptDir -Parent) 'assets'
}

if (-not (Test-Path $AssetsDir)) {
    Write-Host "找不到 assets 目录：$AssetsDir" -ForegroundColor Red
    exit 1
}

$indexPath = Join-Path $AssetsDir '.assets.index'

# 收集所有文件（相对路径，正斜杠），跳过索引文件自己
$rel = @()
Get-ChildItem $AssetsDir -Recurse -File -Force | ForEach-Object {
    $r = $_.FullName.Substring($AssetsDir.Length).TrimStart('\', '/') -replace '\\', '/'
    if ($r -ne '.assets.index') { $rel += $r }
}
$rel = $rel | Sort-Object

# 读旧索引做对比
$old = @()
if (Test-Path $indexPath) {
    $oldLines = [System.IO.File]::ReadAllLines($indexPath, [System.Text.UTF8Encoding]::new($false))
    if ($oldLines.Count -gt 2) { $old = $oldLines[2..($oldLines.Count - 1)] | Sort-Object }
}

$added   = $rel | Where-Object { $old -notcontains $_ }
$removed = $old | Where-Object { $rel -notcontains $_ }

Write-Host "assets 目录：$AssetsDir"
Write-Host ("索引条目：旧 {0} 个 -> 新 {1} 个" -f $old.Count, $rel.Count)
if ($added.Count -gt 0) {
    Write-Host ("新增 {0} 个：" -f $added.Count) -ForegroundColor Green
    $added | ForEach-Object { Write-Host "  + $_" }
}
if ($removed.Count -gt 0) {
    Write-Host ("移除 {0} 个：" -f $removed.Count) -ForegroundColor Yellow
    $removed | ForEach-Object { Write-Host "  - $_" }
}
if ($added.Count -eq 0 -and $removed.Count -eq 0) {
    Write-Host "索引已是最新，无需改动。"
    exit 0
}

if ($WhatIfOnly) {
    Write-Host "（-WhatIfOnly：未写入）"
    exit 0
}

# 写回：版本号 + 数量 + 每行一个路径。
# 用 LF 换行、不加 BOM —— 原文件是 31 0A 31 36 0A... 即 "1\n16\n"，
# 没有 BOM，所以这里也不加。
$sb = New-Object System.Text.StringBuilder
[void]$sb.Append("1`n")
[void]$sb.Append($rel.Count).Append("`n")
foreach ($r in $rel) { [void]$sb.Append($r).Append("`n") }

[System.IO.File]::WriteAllText($indexPath, $sb.ToString(),
                               (New-Object System.Text.UTF8Encoding($false)))

Write-Host "已写入 $indexPath" -ForegroundColor Green
