# =============================================================================
#  把 BBTieba 的已验证逻辑层接入 BBTiebaUI
#
#  设计原则：**逻辑层原样搬过去，不改**。
#  那边是跑通并验证过 462 项断言的部分，任何"顺手改一下"都可能引入回归。
#  这个脚本只做复制和配置合并，不做逻辑修改。
#
#  用法：
#      powershell -ExecutionPolicy Bypass -File tools\port-logic-to-ui.ps1
#      加 -DryRun 只看会做什么，不实际写入
# =============================================================================

param(
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$srcProj = 'D:\BBWorkspace\BBTieba'
$dstProj = 'D:\BBWorkspace\BBTiebaUI'

if (-not (Test-Path $srcProj)) { throw "找不到逻辑层项目 $srcProj" }
if (-not (Test-Path $dstProj)) { throw "找不到 UI 项目 $dstProj" }

function Say($msg) { Write-Host $msg }
function Do-Copy($from, $to) {
    $dir = Split-Path $to -Parent
    if (-not (Test-Path $dir)) {
        if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
        Say ("    mkdir " + $dir)
    }
    if ($DryRun) {
        Say ("    COPY  " + $from.Replace($srcProj, '<src>') + "  ->  " + $to.Replace($dstProj, '<dst>'))
    } else {
        Copy-Item $from $to -Force
        Say ("    copy  " + $to.Replace($dstProj, '<dst>'))
    }
}

Say '=== 1) 复制后端逻辑层（src/tieba/ 全部原样） ==='
$tiebaFiles = Get-ChildItem (Join-Path $srcProj 'src\tieba') -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.hpp') }
foreach ($f in $tiebaFiles) {
    $rel = $f.FullName.Replace((Join-Path $srcProj 'src\'), '')
    Do-Copy $f.FullName (Join-Path $dstProj ("src\" + $rel))
}

Say ''
Say '=== 2) 复制 TiebaController / applicationui / main ==='
foreach ($n in @('TiebaController.cpp', 'TiebaController.hpp', 'applicationui.cpp', 'applicationui.hpp', 'main.cpp')) {
    Do-Copy (Join-Path $srcProj "src\$n") (Join-Path $dstProj "src\$n")
}

Say ''
Say '=== 3) 复制根文件（append.pri / precompiled.h / BBTieba.ts） ==='
Do-Copy (Join-Path $srcProj 'append.pri')     (Join-Path $dstProj 'append.pri')
Do-Copy (Join-Path $srcProj 'precompiled.h')  (Join-Path $dstProj 'precompiled.h')
Do-Copy (Join-Path $srcProj 'BBTieba.ts')     (Join-Path $dstProj 'BBTieba.ts')

Say ''
Say '=== 4) 复制 UI 资源（icons / 用户 QML 先备份到 assets_orig） ==='
$dstAssets = Join-Path $dstProj 'assets'
$backup = Join-Path $dstProj 'assets_orig'
if (-not $DryRun) {
    if (-not (Test-Path $backup)) {
        New-Item -ItemType Directory -Force -Path $backup | Out-Null
        Copy-Item (Join-Path $dstAssets '*') $backup -Recurse -Force
        Say ("    备份原 assets -> " + $backup)
    }
}
$srcIcons = Join-Path $srcProj 'assets\icons'
if (Test-Path $srcIcons) {
    foreach ($f in (Get-ChildItem $srcIcons -File)) {
        Do-Copy $f.FullName (Join-Path $dstAssets ("icons\" + $f.Name))
    }
}

Say ''
Say '=== 5) 复制测试与工具（供开发机回归） ==='
foreach ($n in @('run-tests.ps1', 'check-qml.ps1', 'check-cpp-safety.ps1')) {
    Do-Copy (Join-Path $srcProj "tests\$n") (Join-Path $dstProj "tests\$n")
}
foreach ($n in (Get-ChildItem (Join-Path $srcProj 'tests') -File -Filter 'test_*.cpp')) {
    Do-Copy $n.FullName (Join-Path $dstProj ("tests\" + $n.Name))
}
if (Test-Path (Join-Path $srcProj 'tests\stubs')) {
    foreach ($f in (Get-ChildItem (Join-Path $srcProj 'tests\stubs') -Recurse -File)) {
        $rel = $f.FullName.Replace((Join-Path $srcProj 'tests\'), '')
        Do-Copy $f.FullName (Join-Path $dstProj ("tests\" + $rel))
    }
}
if (Test-Path (Join-Path $srcProj 'tests\data')) {
    foreach ($f in (Get-ChildItem (Join-Path $srcProj 'tests\data') -File)) {
        Do-Copy $f.FullName (Join-Path $dstProj ("tests\data\" + $f.Name))
    }
}
if (Test-Path (Join-Path $srcProj 'build')) {
    foreach ($f in (Get-ChildItem (Join-Path $srcProj 'build') -File)) {
        Do-Copy $f.FullName (Join-Path $dstProj ("build\" + $f.Name))
    }
}

Say ''
Say 'OK 复制完成。下一步由 configure-ui-project.ps1 合并 config.pri / bar-descriptor.xml'
