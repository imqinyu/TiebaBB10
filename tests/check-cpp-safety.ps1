# =============================================================================
#  C++ 静态安全检查
#
#  这个脚本检查的是"在开发机上很难暴露、但真机上会崩溃或静默失效"的模式。
#  每一条都对应一个真实踩过的坑，所以别删规则，删了就会再踩一次。
#
#  用法：
#      powershell -ExecutionPolicy Bypass -File tests\check-cpp-safety.ps1
#  可选参数：指定要检查的目录（默认 src）
# =============================================================================

param(
    [string]$Root
)

$ErrorActionPreference = 'Continue'

if (-not $Root -or $Root -eq '') {
    if ($PSScriptRoot) {
        $Root = Join-Path (Split-Path $PSScriptRoot -Parent) 'src'
    } else {
        $Root = 'src'
    }
}

if (-not (Test-Path $Root)) {
    Write-Host ("SKIP: 找不到目录 " + $Root) -ForegroundColor Yellow
    exit 0
}

$problems = 0
$files = Get-ChildItem $Root -Recurse -File -Include *.cpp, *.hpp

<#
  先把注释剥掉再判断。

  ★ 这一点很关键：我们自己的**注释里就是在讲这些反例**
    （"错的做法：model->clear() 再 append()..."），
    第一版没剥注释，检查器把说明文字当成违规代码、报了假警。

  用逐字符状态机：能正确处理 "a /* b */ c // d" 这种混在一行的情况。
#>
function Get-CodeOnlyLines {
    param([string]$Path)
    $raw = [System.IO.File]::ReadAllLines($Path, [System.Text.UTF8Encoding]::new($false))
    $out = New-Object System.Collections.Generic.List[string]

    $inBlock = $false
    foreach ($line in $raw) {
        $sb = New-Object System.Text.StringBuilder
        $i = 0
        $n = $line.Length
        while ($i -lt $n) {
            if ($inBlock) {
                if ($i + 1 -lt $n -and $line[$i] -eq '*' -and $line[$i + 1] -eq '/') {
                    $inBlock = $false
                    $i += 2
                } else {
                    $i++
                }
                continue
            }
            if ($i + 1 -lt $n -and $line[$i] -eq '/' -and $line[$i + 1] -eq '*') {
                $inBlock = $true
                $i += 2
                continue
            }
            if ($i + 1 -lt $n -and $line[$i] -eq '/' -and $line[$i + 1] -eq '/') {
                break                       # 行注释，整行后面都不要了
            }
            [void]$sb.Append($line[$i])
            $i++
        }
        $out.Add($sb.ToString())
    }
    return $out
}

Write-Host '=== 1) 字符串版 connect 的槽必须存在且签名一致 ===' -ForegroundColor Cyan
<#
  踩过的坑：
    connect(m_manager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(onFinished()));          // ← 槽没有参数
  字符串版 connect 只在**运行时**解析签名，写错时只打一行
      "Object::connect: No such slot ..."
  然后功能完全失效 —— 不崩溃、不报错、日志还是干净的。
  表现就是"图片永远不加载"。

  规则：扫出所有 SLOT(xxx)，若被调用的类里没有同名函数声明（忽略参数），报警。
#>
$slotMiss = 0
foreach ($f in $files) {
    $text = [System.IO.File]::ReadAllText($f.FullName, [System.Text.UTF8Encoding]::new($false))
    $lines = Get-CodeOnlyLines $f.FullName

    # 收集本文件里所有函数声明/定义的函数名
    $declared = @{}
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # 形如:  void Foo::bar(...)   或   void bar(...)
        $m = [regex]::Matches($lines[$i], '(\w+)\s*\(')
        foreach ($mm in $m) { $declared[$mm.Groups[1].Value] = $true }
    }

    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($mm in [regex]::Matches($lines[$i], 'SLOT\(\s*(\w+)\s*\(')) {
            $name = $mm.Groups[1].Value
            if (-not $declared.ContainsKey($name)) {
                Write-Host ('  [' + $f.Name + ':' + ($i + 1) + '] SLOT(' + $name + '(...)) 在本文件里找不到同名函数') -ForegroundColor Yellow
                $slotMiss++
                $problems++
            }
        }
    }
}
if ($slotMiss -eq 0) {
    Write-Host '  OK：所有 SLOT() 引用的函数名都能在本文件里找到'
}

Write-Host ''
Write-Host '=== 2) 增量更新里不能用 clear() + append() 整表重建 ===' -ForegroundColor Cyan
<#
  踩过的坑：
    图片是同时下完好几张的，每次完成都
        model->clear(); model->append(items);
    而 Cascades 的 ListView 正在遍历模型做布局 → use-after-free。
    真机日志： free malloc object that is not allocated:
              /var/build/.../lib/c/alloc/dlist.c:1096

  正确做法：用 ArrayDataModel::replace(i, value) 就地替换变化的那几项。

  ★ 但**不是所有** clear()+append() 都有问题：
      fillModel() 这种"新数据整表重灌"是安全的 —— 它是同步的、由一次请求
      触发，那时没有异步回调在并发碰模型。
      危险的只有**异步回调里的增量更新**（图片下完、分片加载完……）。

    所以这条规则只检查"名字表明是增量更新"的函数：
      refresh*, update*, onXxxReady, onImageReady 之类。
    第一版没做这个区分，把 fillModel 误报了 —— 说明规则写太粗也会浪费注意力。
#>
$modelChurn = 0
foreach ($f in $files) {
    $lines = Get-CodeOnlyLines $f.FullName

    # 先找出所有"增量更新型"函数的行号区间
    $incrRanges = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^[A-Za-z_].*\b(refresh|update)\w*\s*\(.*\)\s*$' -or
            $lines[$i] -match '^[A-Za-z_].*\b(on\w*(Ready|Refreshed|Updated|Finished))\s*\(.*\)\s*$') {
            # 找到函数体结束（花括号配平）
            $depth = 0
            $started = $false
            for ($k = $i; $k -lt $lines.Count; $k++) {
                $depth += ([regex]::Matches($lines[$k], '\{')).Count
                $depth -= ([regex]::Matches($lines[$k], '\}')).Count
                if ($depth -gt 0) { $started = $true }
                if ($started -and $depth -le 0) {
                    $incrRanges.Add([pscustomobject]@{ Start = $i; End = $k; Name = $lines[$i].Trim() })
                    break
                }
            }
        }
    }

    foreach ($r in $incrRanges) {
        for ($i = $r.Start; $i -le $r.End; $i++) {
            if ($lines[$i] -match '(->|\.)clear\s*\(\s*\)') {
                # 同一函数里往后找 append(
                for ($k = $i; $k -le $r.End; $k++) {
                    if ($lines[$k] -match '(->|\.)append\s*\(') {
                        Write-Host ('  [' + $f.Name + ':' + ($i + 1) + '] 增量更新函数里 clear() + append() 整表重建') -ForegroundColor Yellow
                        Write-Host ('      函数: ' + $r.Name) -ForegroundColor Yellow
                        Write-Host ('      改用 ArrayDataModel::replace(i, v) 只替换变化的项；见 onImageReady 的说明') -ForegroundColor Yellow
                        $modelChurn++
                        $problems++
                        break
                    }
                }
            }
        }
    }
}
if ($modelChurn -eq 0) {
    Write-Host '  OK：增量更新函数里没有 clear()+append() 整表重建'
}

Write-Host ''
Write-Host '=== 3) 信号槽里不要用 sender() 取信号参数 ===' -ForegroundColor Cyan
<#
  踩过的坑：
    connect(m_manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(onFinished()));
    槽里用 qobject_cast<QNetworkReply*>(sender()) —— sender() 是**信号发出者**
    （manager），不是信号参数（reply），cast 失败后静默 return。
  规则：槽函数体内同时出现 sender() 和 qobject_cast 时给出提示。
#>
$senderHits = 0
foreach ($f in $files) {
    $lines = Get-CodeOnlyLines $f.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match 'sender\s*\(\s*\)' -and $lines[$i] -match 'qobject_cast') {
            Write-Host ('  [' + $f.Name + ':' + ($i + 1) + '] 用 sender() 做 qobject_cast：确认取的是信号发出者而不是参数') -ForegroundColor Yellow
            $senderHits++
            # 只提示，不算失败（有些场景 sender() 是对的）
        }
    }
}
if ($senderHits -eq 0) {
    Write-Host '  OK：没有 sender()+qobject_cast 的用法'
} else {
    Write-Host ('  提示 ' + $senderHits + ' 处，请人工确认')
}

Write-Host ''
if ($problems -eq 0) {
    Write-Host 'C++ safety check passed.' -ForegroundColor Green
    exit 0
} else {
    Write-Host ('C++ safety check FAILED: ' + $problems + ' problem(s).') -ForegroundColor Red
    exit 1
}
