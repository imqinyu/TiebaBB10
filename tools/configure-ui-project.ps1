# =============================================================================
#  把 BBTiebaUI 配置成"带逻辑层的可编译工程"
#
#  做三件事：
#    1. BBTiebaUI.pro 引入 append.pri（逻辑层源文件靠它自动纳入）
#    2. bar-descriptor.xml 补 access_internet 权限
#    3. config.pri 的 OTHER_FILES 补上新加的 QML / 图标资源
#
#  为什么权限这条特别重要：
#    没有 <permission>access_internet</permission>，BB10 会**静默阻断**所有
#    网络请求 —— 吧列表空白、图片永远下不来，而且日志里看不出原因。
#
#  用法：
#      powershell -ExecutionPolicy Bypass -File tools\configure-ui-project.ps1
# =============================================================================

$ErrorActionPreference = 'Stop'

$proj = Split-Path $PSScriptRoot -Parent

function Say($m) { Write-Host $m }

# -----------------------------------------------------------------------------
Say '=== 1) BBTiebaUI.pro 引入 append.pri ==='
$proFile = Join-Path $proj 'BBTiebaUI.pro'
$proText = [System.IO.File]::ReadAllText($proFile, [System.Text.UTF8Encoding]::new($false))
if ($proText -notmatch 'append\.pri') {
    $proText = $proText.TrimEnd() + @'


# 项目级额外配置：zlib / libbbsystem / 头文件路径 / 逻辑层源文件兜底。
# config.pri 由 Momentics IDE 自动生成且会被重写，所以长期配置放 append.pri。
include(append.pri)
'@
    [System.IO.File]::WriteAllText($proFile, $proText + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
    Say '    已加入 include(append.pri)'
} else {
    Say '    已有 include(append.pri)，跳过'
}

# -----------------------------------------------------------------------------
Say ''
Say '=== 2) bar-descriptor.xml 补 access_internet 权限 ==='
$descFile = Join-Path $proj 'bar-descriptor.xml'
$descText = [System.IO.File]::ReadAllText($descFile, [System.Text.UTF8Encoding]::new($false))
if ($descText -notmatch 'access_internet') {
    $needle = '    <permission system="true">run_native</permission>'
    if ($descText.Contains($needle)) {
        $replacement = @'
    <permission system="true">run_native</permission>
    <!-- ★ 必须项：没有它 BB10 会静默阻断所有网络请求，
         表现为吧列表空白、图片永远下不来，日志里还看不出原因。 -->
    <permission>access_internet</permission>
'@
        $descText = $descText.Replace($needle, $replacement.TrimEnd())
        [System.IO.File]::WriteAllText($descFile, $descText, (New-Object System.Text.UTF8Encoding($false)))
        Say '    已补 access_internet'
    } else {
        Say '    !! 找不到 run_native 行，请手工确认'
    }
} else {
    Say '    已有 access_internet，跳过'
}

# -----------------------------------------------------------------------------
Say ''
Say '=== 3) config.pri 的 OTHER_FILES 补资源 ==='
$cfgFile = Join-Path $proj 'config.pri'
$cfgText = [System.IO.File]::ReadAllText($cfgFile, [System.Text.UTF8Encoding]::new($false))

# 期望纳入的资源（icon 目录 + 根下 qml）
$assets = @()
$iconDir = Join-Path $proj 'assets\icons'
if (Test-Path $iconDir) {
    $assets += Get-ChildItem $iconDir -File | ForEach-Object { 'assets/icons/' + $_.Name }
}
$assets += Get-ChildItem (Join-Path $proj 'assets') -File -Filter '*.qml' | ForEach-Object { 'assets/' + $_.Name }
$assets += Get-ChildItem (Join-Path $proj 'assets') -File -Filter '*.png' | ForEach-Object { 'assets/' + $_.Name }

$missing = @()
foreach ($a in $assets) {
    # OTHER_FILES 里以文件名出现就算有了（IDE 生成的是全路径）
    $leaf = Split-Path $a -Leaf
    if ($cfgText -notmatch [regex]::Escape($leaf)) { $missing += $a }
}

if ($missing.Count -eq 0) {
    Say '    资源都已列入，跳过'
} else {
    # 找到 config_pri_assets 块，把最后一个 OTHER_FILES 条目补上续行符再追加
    $lines = [System.IO.File]::ReadAllLines($cfgFile, [System.Text.UTF8Encoding]::new($false))
    $out = New-Object System.Collections.Generic.List[string]
    $inAssetsBlock = $false
    $lastOtherIdx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*config_pri_assets\s*\{') { $inAssetsBlock = $true }
        if ($inAssetsBlock -and $lines[$i] -match '^\s*OTHER_FILES\s*\+=') { }
        if ($inAssetsBlock -and $lines[$i] -match '\$\$quote\(\$\$BASEDIR/assets/') { $lastOtherIdx = $out.Count }
        if ($inAssetsBlock -and $lines[$i] -match '^\s*\}\s*$') { $inAssetsBlock = $false }
        $out.Add($lines[$i])
    }

    if ($lastOtherIdx -lt 0) {
        Say '    !! config.pri 里找不到 resources 列表，请让 IDE 刷新项目'
    } else {
        # 给最后一条补续行符
        if ($out[$lastOtherIdx] -notmatch '\\\s*$') { $out[$lastOtherIdx] = $out[$lastOtherIdx] + ' \' }
        $add = New-Object System.Collections.Generic.List[string]
        foreach ($a in $missing) {
            $add.Add('        $$quote($$BASEDIR/' + $a + ') \')
        }
        # 最后一条去掉续行符
        $add[$add.Count - 1] = $add[$add.Count - 1] -replace '\\\s*$', ''
        $out.InsertRange($lastOtherIdx + 1, $add)
        [System.IO.File]::WriteAllLines($cfgFile, $out, (New-Object System.Text.UTF8Encoding($false)))
        Say ('    已追加 ' + $missing.Count + ' 项：')
        foreach ($a in $missing) { Say ('      ' + $a) }
    }
}

Say ''
Say 'OK 配置完成。'
