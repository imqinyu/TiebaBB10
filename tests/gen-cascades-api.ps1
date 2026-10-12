# =============================================================================
#  gen-cascades-api.ps1
#
#  从 BB10 NDK 头文件里提取 Cascades 的**属性名**和**信号名**，
#  生成 tests\cascades-api.txt，供 check-qml.ps1 做存在性校验。
#
#  为什么需要：
#    "编造一个听起来很合理的属性名"这类错误我犯了两次：
#        emptyText   (ListView 没这个属性)
#        onSelected  (Option 的 selected 只是属性，没有信号)
#    两次的代价都是**整个 QML 页面加载失败**（真机黑屏 / 点不进去）。
#    黑名单只能抓已经踩过的；这份清单能做正向校验。
#
#  什么时候要重新生成：
#    升级 NDK / 换 target 版本时。平时不用。
#
#  用法：
#      powershell -ExecutionPolicy Bypass -File tests\gen-cascades-api.ps1
# =============================================================================

$ErrorActionPreference = 'Stop'

# NDK 的 Cascades 头文件目录（可按需改）
$inc = 'C:\NDKFiles\target_10_3_1_995\qnx6\usr\include\bb\cascades'

if (-not (Test-Path $inc)) {
    Write-Host ("找不到 Cascades 头文件目录: " + $inc) -ForegroundColor Red
    Write-Host '请确认 NDK 路径（QNX_TARGET\usr\include\bb\cascades）'
    exit 1
}

$props = @{}
$signals = @{}

foreach ($f in (Get-ChildItem $inc -Recurse -Filter '*.h')) {
    $txt = [System.IO.File]::ReadAllText($f.FullName, [System.Text.UTF8Encoding]::new($false))

    <#
      Q_PROPERTY 的写法很杂（类型里可能有 :: < > * 和空格，还可能跨行）：
          Q_PROPERTY(QString title READ title WRITE setTitle ...)
          Q_PROPERTY(bb::cascades::DataModel *dataModel READ dataModel ...)
          Q_PROPERTY(QDeclarativeListProperty<bb::cascades::AbstractActionItem> actions ...)
      正则很容易漏。**更稳的办法**：先取出 Q_PROPERTY 括号里的内容，
      截到第一个 READ/WRITE/RESET/NOTIFY/MEMBER 关键字为止，
      剩下部分的**最后一个标识符**就是属性名。
      （第一版用正则，漏了 dataModel / listItemComponents，造成误报。）
    #>
    foreach ($m in [regex]::Matches($txt, 'Q_PROPERTY\s*\(([^)]*)\)', 'Singleline')) {
        $body = $m.Groups[1].Value
        $cut = [regex]::Match($body, '\b(READ|WRITE|RESET|NOTIFY|MEMBER|CONSTANT|FINAL|REVISION)\b')
        if ($cut.Success) { $body = $body.Substring(0, $cut.Index) }
        $ids = [regex]::Matches($body, '[A-Za-z_]\w*')
        if ($ids.Count -ge 1) {
            $props[$ids[$ids.Count - 1].Value] = 1
        }
    }

    # Q_SIGNALS: 段里的 void name(
    $inSig = $false
    foreach ($line in ($txt -split "`n")) {
        if ($line -match 'Q_SIGNALS\s*:|^\s*signals\s*:') { $inSig = $true; continue }
        if ($line -match '^\s*(public|private|protected)\s*(slots)?\s*:|^\s*Q_SLOTS\s*:') { $inSig = $false }
        if ($inSig -and $line -match '\bvoid\s+(\w+)\s*\(') {
            $signals[$matches[1]] = 1
        }
    }
}

$out = New-Object System.Collections.Generic.List[string]
$out.Add('# Cascades QML 属性/信号清单（从 NDK 头文件自动提取）')
$out.Add('# 生成时间: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
$out.Add('# 来源: ' + $inc)
$out.Add('# 用途: check-qml.ps1 第 8b 条规则 —— 校验 QML 里的属性名/信号处理器是否存在')
$out.Add('# 重新生成: powershell -ExecutionPolicy Bypass -File tests\gen-cascades-api.ps1')
$out.Add('')
$out.Add('[properties]')
foreach ($k in ($props.Keys | Sort-Object)) { $out.Add($k) }
$out.Add('')
$out.Add('[signals]')
foreach ($k in ($signals.Keys | Sort-Object)) { $out.Add($k) }

$target = Join-Path $PSScriptRoot 'cascades-api.txt'
[System.IO.File]::WriteAllLines($target, $out, (New-Object System.Text.UTF8Encoding($false)))

Write-Host ('已生成 ' + $target) -ForegroundColor Green
Write-Host ('  属性 ' + $props.Count + ' 个，信号 ' + $signals.Count + ' 个')
