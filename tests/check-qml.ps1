# =============================================================================
#  check-qml.ps1 - Cascades QML 静态检查
#
#  为什么需要：
#    Cascades 的 QML 是 Qt Quick 的一个**子集**，但顶层语法看着很像，
#    很容易顺手写下 Qt Quick 专有的类型/属性。这类错误的代价特别高：
#    整个 QML 加载失败 → 黑屏，而日志只给一行
#        error when loading QML from: QUrl("asset:///main.qml")
#        --- errors: (asset:///main.qml:363:29: Repeater is not a type)
#    然后要在真机上往返一次才能发现。
#
#  这个脚本把已知的坑列出来做静态检查，让错误在开发机上就暴露。
#
#  用法： .\tests\check-qml.ps1
# =============================================================================

$ErrorActionPreference = 'Stop'

<#
  允许两种用法：
      check-qml.ps1                 -> 扫描 assets\ 下**所有** .qml
      check-qml.ps1 <文件路径>       -> 只查指定文件（自测用）

  ★ 为什么默认要扫全部：
    项目里已经不止 main.qml 了（还有 InBaPage / ThreadPage / SubPostsPage / BaItem
    以及各类列表项组件）。只查 main.qml 会漏掉 push 进来的页面 ——
    而那些页面同样会因为一个 Repeater 或一处越界访问就整页打不开。
#>
$qmlFiles = @()
if ($args.Count -ge 1 -and -not [string]::IsNullOrEmpty($args[0])) {
    $qmlFiles = @($args[0])
} else {
    $repo = Split-Path -Parent $PSScriptRoot
    $assetsDir = Join-Path $repo 'assets'
    if (Test-Path $assetsDir) {
        $qmlFiles = Get-ChildItem $assetsDir -Recurse -File -Filter '*.qml' |
            ForEach-Object { $_.FullName }
    }
}

if ($qmlFiles.Count -eq 0) {
    Write-Host ('check-qml: 没找到 .qml 文件（assets 目录: ' + (Join-Path (Split-Path -Parent $PSScriptRoot) 'assets') + ')') -ForegroundColor Red
    exit 1
}

# 逐个文件检查；把所有文件的文本拼起来做全局性检查（括号配平按文件单独算更准，
# 所以下面括号那一节是按文件循环的）
$allProblems = 0
foreach ($one in $qmlFiles) {

$qmlPath = $one
if (-not (Test-Path $qmlPath)) {
    Write-Host ('check-qml: not found: ' + $qmlPath) -ForegroundColor Red
    $allProblems++
    continue
}

Write-Host ('----- ' + (Split-Path $qmlPath -Leaf) + ' -----') -ForegroundColor DarkGray

# 每个文件独立计数，否则第一个文件的问题会算到后面的文件头上
$problems = 0

$text = [System.IO.File]::ReadAllText($qmlPath, [System.Text.UTF8Encoding]::new($false))
$lines = [System.IO.File]::ReadAllLines($qmlPath, [System.Text.UTF8Encoding]::new($false))

# ---------------------------------------------------------------------------
# 1) Qt Quick 专有类型 —— Cascades 里不存在，用了就黑屏
# ---------------------------------------------------------------------------
$qtQuickOnly = @(
    'Repeater',
    'Rectangle',
    'MouseArea',
    'Flickable',
    'Connections',
    'Column',
    'Row',
    'Grid',
    'Flow',
    'Loader',
    'Canvas',
    'TextEdit',
    'TextInput',
    'Animation',
    'NumberAnimation',
    'PropertyAnimation',
    'SequentialAnimation',
    'ParallelAnimation',
    'Timer'
)

$problems = 0

function Check-TypeUsage {
    param([string]$typeName)

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]

        # 跳过注释行
        $trimmed = $line.TrimStart()
        if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')) {
            continue
        }
        # 跳过行尾注释里的提及
        $code = $line
        $commentAt = $code.IndexOf('//')
        if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

        # 匹配「类型名 + 可选空白 + {」这种声明式用法，
        # 且前面不是标识符字符（避免 ListItemComponent 里匹配到 Component）
        $pattern = '(?<![A-Za-z0-9_.])' + [regex]::Escape($typeName) + '\s*\{'
        if ($code -match $pattern) {
            Write-Host ('  [' + ($i + 1) + '] Qt Quick 专有类型 ' + $typeName + '：' + $line.Trim()) -ForegroundColor Yellow
            $script:problems++
        }
    }
}

Write-Host '=== 1) Qt Quick 专有类型检查 ===' -ForegroundColor Cyan
foreach ($t in $qtQuickOnly) {
    Check-TypeUsage -typeName $t
}
if ($problems -eq 0) {
    Write-Host '  OK：没有使用 Cascades 不支持的 Qt Quick 类型'
}

# ---------------------------------------------------------------------------
# 2) StackListLayout 不支持 orientation
#    （orientation 是 StackLayout 的属性，两者名字很像，极易写错）
# ---------------------------------------------------------------------------
Write-Host '=== 2) StackListLayout 误用 orientation 检查 ===' -ForegroundColor Cyan
$listOrientation = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'StackListLayout\s*\{[^}]*orientation') {
        Write-Host ('  [' + ($i + 1) + '] StackListLayout 没有 orientation 属性：' + $lines[$i].Trim()) -ForegroundColor Yellow
        $listOrientation++
        $problems++
    }
}
if ($listOrientation -eq 0) {
    Write-Host '  OK：StackListLayout 未使用 orientation'
}

# ---------------------------------------------------------------------------
# 3) ListView 必须有 listItemComponents（且 type 与 dataModel 的键一致）
# ---------------------------------------------------------------------------
Write-Host '=== 3) ListView 结构检查 ===' -ForegroundColor Cyan
$lvCount = ([regex]::Matches($text, '(?<![A-Za-z0-9_])ListView\s*\{')).Count
$licCount = ([regex]::Matches($text, 'ListItemComponent\s*\{')).Count
Write-Host ('  ListView ' + $lvCount + ' 个，ListItemComponent ' + $licCount + ' 个')
if ($lvCount -gt 0 -and $licCount -lt $lvCount) {
    Write-Host '  WARN：有 ListView 缺少 listItemComponents（会显示空白列表）' -ForegroundColor Yellow
    $problems++
} else {
    Write-Host '  OK：每个 ListView 都配了 ListItemComponent'
}

# ListItemComponent 的 type 必须是 "" 或与数据里的键一致
$badType = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'type:\s*"([^"]*)"') {
        $t = $Matches[1]
        if ($t -ne '' -and $t -ne 'item' -and $t -ne 'header') {
            Write-Host ('  [' + ($i + 1) + '] ListItemComponent type="' + $t + '"（数据是 QVariantMap，type 应为空串）') -ForegroundColor Yellow
            $badType++
            $problems++
        }
    }
}
if ($badType -eq 0) {
    Write-Host '  OK：ListItemComponent 的 type 都是空串'
}

# ---------------------------------------------------------------------------
# 4) 括号配平
# ---------------------------------------------------------------------------
Write-Host '=== 4) 括号配平 ===' -ForegroundColor Cyan
<#
  ★ 数括号前必须先剥掉注释。

  踩过的坑：注释里写了反例和示例（例如 "(...)"、""），
  第一版直接数整个文件，把注释里的括号也算进去，于是报
      "FAIL：( 6 个，) 8 个，不配平"
  而代码本身完全正常 —— 假警比漏报更浪费时间，因为它让人开始怀疑好代码。
#>
$codeOnly = New-Object System.Text.StringBuilder
$inBlock = $false
foreach ($ln in $lines) {
    $i2 = 0
    $n2 = $ln.Length
    while ($i2 -lt $n2) {
        if ($inBlock) {
            if ($i2 + 1 -lt $n2 -and $ln[$i2] -eq '*' -and $ln[$i2 + 1] -eq '/') { $inBlock = $false; $i2 += 2 }
            else { $i2++ }
            continue
        }
        if ($i2 + 1 -lt $n2 -and $ln[$i2] -eq '/' -and $ln[$i2 + 1] -eq '*') { $inBlock = $true; $i2 += 2; continue }
        if ($i2 + 1 -lt $n2 -and $ln[$i2] -eq '/' -and $ln[$i2 + 1] -eq '/') { break }
        [void]$codeOnly.Append($ln[$i2])
        $i2++
    }
    [void]$codeOnly.Append("`n")
}
$codeText = $codeOnly.ToString()

$pairs = @(@('{', '}'), @('(', ')'), @('[', ']'))
foreach ($p in $pairs) {
    $open = ([regex]::Matches($codeText, [regex]::Escape($p[0]))).Count
    $close = ([regex]::Matches($codeText, [regex]::Escape($p[1]))).Count
    if ($open -eq $close) {
        Write-Host ('  OK：' + $p[0] + ' ' + $open + ' 对 ' + $p[1] + ' ' + $close)
    } else {
        Write-Host ('  FAIL：' + $p[0] + ' ' + $open + ' 个，' + $p[1] + ' ' + $close + ' 个，不配平') -ForegroundColor Red
        $problems++
    }
}

# ---------------------------------------------------------------------------
# 5) QVariantMap 属性不能在 undefined 上取属性（会导致 SIGSEGV）
# ---------------------------------------------------------------------------
Write-Host '=== 5) 在空 map 上取属性的风险 ===' -ForegroundColor Cyan
$risky = 0
foreach ($prop in @('pageInfo', 'forumInfo', 'threadInfo', 'errorDetail')) {
    $pattern = 'tieba\.' + $prop + '\.'
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        $trimmed = $line.TrimStart()
        if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*')) { continue }
        $code = $line
        $commentAt = $code.IndexOf('//')
        if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }
        if ($code -match $pattern) {
            # 允许出现在安全函数内部的写法（带 && 判断或三元判断）
            if ($code -match '&&' -or $code -match '\?\s*tieba\.') {
                continue
            }
            Write-Host ('  [' + ($i + 1) + '] 裸访问 tieba.' + $prop + '（可能 SIGSEGV）：' + $line.Trim()) -ForegroundColor Yellow
            $risky++
            $problems++
        }
    }
}
if ($risky -eq 0) {
    Write-Host '  OK：没有裸访问可能为空的 map 属性'
}

# ---------------------------------------------------------------------------
# 6) ListItemComponent 里不能直接访问 context property（tieba.xxx）
#
#    ListItemComponent 是**独立上下文**，那里访问不到页面注入的 context
#    property `tieba`，运行时只报：
#        asset:///main.qml:401: ReferenceError: Can't find variable: tieba
#    正确做法：把逻辑放到根节点的函数里，ListItemComponent 里调 rootPage.xxx，
#    或者用 ListView 的 onTriggered（那个在页面作用域里）。
# ---------------------------------------------------------------------------
Write-Host '=== 6) ListItemComponent 内的 tieba. 访问检查 ===' -ForegroundColor Cyan
$licLeak = 0
$inLic = $false
$depth = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]

    if (-not $inLic -and $line -match 'ListItemComponent\s*\{') {
        $inLic = $true
        $depth = 1
        continue
    }
    if ($inLic) {
        $depth += ([regex]::Matches($line, '\{')).Count
        $depth -= ([regex]::Matches($line, '\}')).Count

        # 去掉注释再判断
        $code = $line
        $commentAt = $code.IndexOf('//')
        if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }
        if ($code.TrimStart().StartsWith('*')) { $code = '' }

        if ($code -match '(?<![A-Za-z0-9_.])tieba\.') {
            Write-Host ('  [' + ($i + 1) + '] ListItemComponent 内访问 tieba.（会 ReferenceError）：' + $line.Trim()) -ForegroundColor Yellow
            $licLeak++
            $problems++
        }

        if ($depth -le 0) { $inLic = $false }
    }
}
if ($licLeak -eq 0) {
    Write-Host '  OK：ListItemComponent 内没有直接访问 tieba.'
}

# ---------------------------------------------------------------------------
# 7) 只有 NavigationPane 能 push —— Page 上没有 push()
#
#    踩过的坑：
#      NavigationPane::push() 的签名是 push(bb::cascades::Page*)，
#      只接受 Page；而且 **Page 上没有任何 push()/navigationPane 接口**
#      （真机头文件 bb/cascades/page.h 里零匹配）。
#      我一开始把被 push 的页面根写成 NavigationPane，还用了 page.push(...)，
#      两处都是运行时才炸的类型错误。
#
#    规则：
#      - 收集本文件里所有 NavigationPane 的 id
#      - 扫所有 `xxx.push(` ，如果 xxx 既不是 NavigationPane 的 id、
#        也不是显式声明为 variant 的属性（说明是外部传入的栈引用），就报警
# ---------------------------------------------------------------------------
Write-Host '=== 7) push() 的接收者必须是 NavigationPane ===' -ForegroundColor Cyan
$navIds = @{}
# 用剥注释后的文本找 NavigationPane { ... id: xxx } 的配对
$navRx = [regex]'NavigationPane\s*\{[\s\S]{0,300}?id\s*:\s*(\w+)'
foreach ($m in $navRx.Matches($codeText)) { $navIds[$m.Groups[1].Value] = $true }

# 声明为 variant 的属性名（这些是从外部传入的导航栈引用，放行）
$variantProps = @{}
foreach ($m in [regex]::Matches($codeText, 'property\s+variant\s+(\w+)')) {
    $variantProps[$m.Groups[1].Value] = $true
}

$pushBad = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $trimmed = $line.TrimStart()
    if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')) { continue }
    $code = $line
    $commentAt = $code.IndexOf('//')
    if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

    foreach ($m in [regex]::Matches($code, '(?<![A-Za-z0-9_\.])(\w+)\.push\s*\(')) {
        $recv = $m.Groups[1].Value

        <#
          只看"像导航栈"的接收者：
            - 已声明为 variant 的属性（外部传入的栈引用）  → 合法
            - 是 NavigationPane 的 id                       → 合法
            - 其它以 Nav/nav/Pane/pane/Page 结尾的          → 报警（多半把容器当栈了）
            - 其余（parts、arr、list 之类）                 → 是 JS 数组 push，忽略

          为什么要区分：JavaScript 数组也有 push()，
          `var parts = []; parts.push(x)` 第一版被误报成 7 个违规（纯假警）；
          而真正的坑恰好是把 Page 当导航栈用（Page 上没有 push）。
        #>
        if ($variantProps.ContainsKey($recv)) { continue }
        if ($navIds.ContainsKey($recv)) { continue }
        if ($recv -notmatch '(Nav|nav|Pane|pane|Page|page)$') { continue }

        Write-Host ('  [' + ($i + 1) + '] ' + $recv + '.push(...)：接收者不是 NavigationPane') -ForegroundColor Yellow
        Write-Host ('      Page 上没有 push()；只有 NavigationPane 有 push(Page*)。') -ForegroundColor Yellow
        Write-Host ('      被 push 的页面根要用 Page；导航栈引用请声明为 property variant。') -ForegroundColor Yellow
        $pushBad++
        $problems++
    }
}
if ($pushBad -eq 0) {
    Write-Host ('  OK：push() 的接收者都是 NavigationPane（本文件识别到 ' + $navIds.Count + ' 个）')
}

# ---------------------------------------------------------------------------
# 8) 已知不存在的属性名 —— "编造属性"检测
#
#    踩过的坑（不止一次）：
#      我给 ListView 加了 `emptyText:`，想着"空列表提示"应该有这么个属性。
#      真机直接炸：
#          asset:///main.qml:102:21: Cannot assign to non-existent property "emptyText"
#          QmlDocument:createRootObject document is not loaded or has errors
#      → **整个根节点创建失败**，App 起不来。比单个控件不显示严重得多。
#      （查 bb/cascades/listview.h 确认：ListView 没有 empty* 任何属性。）
#
#    为什么之前的检查器查不出来：它只查"类型是否存在"，不查"属性是否存在"。
#    完整校验需要 QML 类型系统，这里做不到；但**把已经踩过的坑记成名单**是
#    够用的 —— 这类错误的特征是"名字听起来很合理"，所以就容易反复犯。
# ---------------------------------------------------------------------------
Write-Host '=== 8) 已知不存在的属性名 ===' -ForegroundColor Cyan

# 属性名 -> 正确做法（都是我实际用过或查证过的）
$badProps = @{
    'emptyText' = 'ListView 没有这个属性；空列表提示用 Container(DockLayout) + 覆盖 Label，visible 绑 model.isEmpty()'
    'onSelected' = 'Option 没有 selected 信号（selected 只是普通属性，没有 NOTIFY）；监听分段切换请用 TitleBar 的 onSelectedIndexChanged'
}

$badPropHits = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    $trimmed = $lines[$i].TrimStart()
    if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')) { continue }
    $code = $lines[$i]
    $commentAt = $code.IndexOf('//')
    if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

    foreach ($p in $badProps.Keys) {
        # 形如 `emptyText:` 的属性赋值
        if ($code -match ('(?<![A-Za-z0-9_])' + [regex]::Escape($p) + '\s*:')) {
            Write-Host ('  [' + ($i + 1) + '] 使用了不存在的属性 ' + $p + '：' + $lines[$i].Trim()) -ForegroundColor Yellow
            Write-Host ('      ' + $badProps[$p]) -ForegroundColor Yellow
            $badPropHits++
            $problems++
        }
    }
}
if ($badPropHits -eq 0) {
    Write-Host '  OK：没有使用已知不存在的属性名'
}

# ---------------------------------------------------------------------------
# 8c) 组件级 on* 处理器**不能**用来监听 tieba（context property）的信号
#
#    踩过的坑（真机原文，两次）：
#        Cannot assign to non-existent property "onLoggedInChanged"
#        Cannot assign to non-existent property "onLoginChanged"
#        QmlDocument:createRootObject ... can't create root   → 黑屏
#
#    我第一版把这条规则写成了"信号名必须拼对"（loggedInChanged → loginChanged），
#    那是**治标**：第二次把名字拼对了（onLoginChanged 确实是 tieba 的信号名），
#    **照样报错**。
#
#    真正的原因是：
#      QML 的 `onXxx:` 处理器要求**该组件自己**有 xxx 信号。
#      `tieba` 是 setContextProperty 注入的**另一个对象**，
#      它的信号属于它自己，不属于 TabbedPane/Page/Container……
#      所以不管名字拼得多准，组件级 on* 都合成不出来。
#      （Cascades 也没有 Qt Quick 的 Connections 类型可用。）
#
#    规则：只要组件级 on* 的名字**命中 tieba 的信号名**，就判失败 ——
#    因为那正是"想监听 tieba 但写法错了"的特征。
#
#    正确做法：把这类逻辑放到 C++（例如 TiebaController::init() 里驱动），
#    或者由 QML 主动调用 tieba 的方法。
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# 8d) contextActions 必须是**数组**（**会判失败**）
#
#    踩过的坑（真机原文，四页全挂）：
#        bb::cascades::QmlDocument:createRootObject document is not loaded or has errors
#        → 进吧详情空白、贴子点不进去
#
#    原因：我写成了
#        contextActions: ActionSet { ... }
#    但官方文档（bb/cascades/resources/actionset.h 与 controls/listview.h）里的
#    用法是**数组**：
#        contextActions: [
#            ActionSet {
#                ActionItem { title: "Reply" }
#            }
#        ]
#    少了方括号 → QML 解析失败 → 该文件所有 createObject() 都失败。
#
#    这条规则扫"contextActions 后面直接跟标识符"的写法并判失败，
#    因为这类错误的代价是**整页加载失败**，必须拦住。
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# 8e) ActionItem 不能写 visible（**会判失败**）
#
#    真机原文：
#        asset:///ThreadPage.qml:228:33: Cannot assign to non-existent property "visible"
#        bb::cascades::QmlDocument:createRootObject document is not loaded or has errors
#        → 贴子页整页打不开（"帖子点不进去"）
#
#    原因：bb/cascades/resources/abstractactionitem.h 里**没有** visible，
#    只有 title / image / imageSource / enabled / accessibility /
#    builtInShortcutsEnabled / backgroundColor / defaultAction / shortcuts。
#    想表达"这一项不可用"要用 **enabled**。
#
#    ★ 为什么 8b 的清单没拦住：visible **确实是** Cascades 的属性
#      （属于别的类）。按名字的清单无法判断"这个属性属于哪个类"。
#      所以这里用位置判断：在 ActionItem { ... } 块里出现 visible 就是错的。
# ---------------------------------------------------------------------------
Write-Host '=== 8e) ActionItem 不能写 visible ===' -ForegroundColor Cyan

$aiBad = 0
$inActionItem = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    $trimmed = $lines[$i].TrimStart()
    if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')) { continue }
    $code = $lines[$i]
    $commentAt = $code.IndexOf('//')
    if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

    # 粗略跟踪 ActionItem 块（按行首的 ActionItem { 与其后的 visible）
    if ($code -match '^\s*ActionItem\s*\{') { $inActionItem++ ; continue }
    if ($inActionItem -gt 0 -and $code -match 'visible\s*:') {
        Write-Host ('  [' + ($i + 1) + '] ActionItem 里写了 visible —— 这个属性不存在') -ForegroundColor Red
        Write-Host '      ActionItem 只有 title/image/imageSource/enabled/...，' -ForegroundColor Red
        Write-Host '      想表达"不可用"请用 enabled。误用会让**整个 QML 加载失败**。' -ForegroundColor Red
        $aiBad++
        $problems++
    }
    # 块结束（粗略：遇到同级或更浅的 } 就退出）
    if ($inActionItem -gt 0 -and $code -match '^\s*\}\s*$') { $inActionItem-- }
}
if ($aiBad -eq 0) {
    Write-Host '  OK：ActionItem 里没有使用 visible'
}
Write-Host '=== 8d) contextActions 必须是数组 ===' -ForegroundColor Cyan

$caBad = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    $trimmed = $lines[$i].TrimStart()
    if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')) { continue }
    $code = $lines[$i]
    $commentAt = $code.IndexOf('//')
    if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

    # contextActions: <标识符>  —— 合法的应该是 contextActions: [
    if ($code -match 'contextActions\s*:\s*([A-Za-z_]\w*)') {
        Write-Host ('  [' + ($i + 1) + '] contextActions: ' + $matches[1] + '  —— 缺方括号') -ForegroundColor Red
        Write-Host '      官方用法是数组：contextActions: [ ActionSet { ... } ]' -ForegroundColor Red
        Write-Host '      写成 contextActions: ActionSet {...} 会让**整个 QML 加载失败**' -ForegroundColor Red
        $caBad++
        $problems++
    }
}
if ($caBad -eq 0) {
    Write-Host '  OK：contextActions 都用了数组形式'
}
Write-Host '=== 8c) 不能监听 context property（tieba）的信号 ===' -ForegroundColor Cyan

# 从 src/TiebaController.hpp 的 signals: 段抄来的真实信号名
$tiebaSignals = @(
    'forumsChanged','threadsChanged','postsChanged','subPostsChanged',
    'forumInfoChanged','threadInfoChanged','pageInfoChanged',
    'loadingChanged','errorMessageChanged','errorDetailChanged','lastRawChanged',
    'currentForumNameChanged','currentThreadIdChanged','hasMoreChanged',
    'loginChanged','lastLoginErrorChanged','requestFinished','imageCacheChanged'
)
$tiebaSignalSet = @{}
foreach ($s in $tiebaSignals) { $tiebaSignalSet[$s] = 1 }

# tieba 的属性名 —— 用来识别"照着属性名编的处理器"（loggedIn → onLoggedInChanged）
$tiebaProps = @(
    'forums','threads','posts','subPosts','forumInfo','threadInfo','pageInfo',
    'loading','errorMessage','errorDetail','lastRaw','currentForumName',
    'currentThreadId','hasMore','loggedIn','nickName','lastLoginError',
    'imageCacheVersion'
)
$tiebaPropSet = @{}
foreach ($p in $tiebaProps) { $tiebaPropSet[$p] = 1 }

$sigBad = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    $trimmed = $lines[$i].TrimStart()
    if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')) { continue }
    $code = $lines[$i]
    $commentAt = $code.IndexOf('//')
    if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

    foreach ($m in [regex]::Matches($code, '(?<![A-Za-z0-9_])on(\w+)\s*:')) {
        $handler = $m.Groups[1].Value
        $sigName = $handler.Substring(0, 1).ToLower() + $handler.Substring(1)

        $isTiebaSignal = $tiebaSignalSet.ContainsKey($sigName)

        # 也识别"照着属性名编的"（loggedInChanged → loggedIn）
        $base = $sigName
        if ($base.EndsWith('Changed')) { $base = $base.Substring(0, $base.Length - 7) }
        $looksLikePropName = $tiebaPropSet.ContainsKey($base)

        if (-not $isTiebaSignal -and -not $looksLikePropName) { continue }

        Write-Host ('  [' + ($i + 1) + '] on' + $handler + '：组件级 on* **监听不到 tieba 的信号**') -ForegroundColor Red
        if ($isTiebaSignal) {
            Write-Host ('      名字其实是对的（tieba 的信号确实叫 ' + $sigName + '），') -ForegroundColor Red
            Write-Host ('      但 tieba 是 context property，它的信号不属于本组件。') -ForegroundColor Red
        } else {
            Write-Host ('      tieba 里没有 ' + $sigName + ' 这个信号（' + $base + ' 是属性名，不是信号名）。') -ForegroundColor Red
        }
        Write-Host ('      QML 的 onXxx 要求组件**自己**有 xxx 信号；Cascades 也没有 Connections。') -ForegroundColor Red
        Write-Host ('      → 请把这段逻辑放到 C++（例如 TiebaController::init()），') -ForegroundColor Red
        Write-Host ('        或改成用户操作时由 QML 主动调用 tieba.xxx()。') -ForegroundColor Red
        $sigBad++
        $problems++
    }
}
if ($sigBad -eq 0) {
    Write-Host ('  OK：模块级 on* 处理器名都对得上（清单 ' + $tiebaSignals.Count + ' 个信号）')
}

# ---------------------------------------------------------------------------
# 8b) 属性/信号的**存在性**校验（对着 NDK 头文件提取的清单）
#
#    为什么加这条：上面的 8) 是"黑名单"，只能抓我已经踩过的坑。
#    同类错误我犯了两次（emptyText、onSelected），说明靠记名单不够。
#
#    这里用 tests/cascades-api.txt（从 NDK 头文件自动提取的
#    493 个属性名 + 515 个信号名）做正向校验：
#      - `foo:` 形式的属性赋值，foo 要在清单里 或是本项目自定义属性
#      - `onFoo:` 形式的信号处理器，foo 要是小写开头的已知信号
#        （QML 规则：信号 fooBar → onFooBar）
#
#    未识别的情况**只提示、不判失败** —— QML 允许自定义属性、
#    attached property（ActionBar.placement），提取也可能有遗漏。
#    它的价值是：如果里面冒出你没定义过的名字，那基本就是编造的属性名，
#    而真机上这类错误会让**整个页面加载失败**。
# ---------------------------------------------------------------------------
Write-Host '=== 8b) 属性/信号名核对【仅提示，不判失败】===' -ForegroundColor Cyan

$apiFile = Join-Path $PSScriptRoot 'cascades-api.txt'
if (-not (Test-Path $apiFile)) {
    Write-Host '  SKIP：没有 tests/cascades-api.txt（跑 tests\gen-cascades-api.ps1 生成）' -ForegroundColor DarkGray
} else {
    $apiProps = @{}
    $apiSignals = @{}
    $section = ''
    foreach ($l in [System.IO.File]::ReadAllLines($apiFile, [System.Text.UTF8Encoding]::new($false))) {
        if ($l -eq '[properties]') { $section = 'p'; continue }
        if ($l -eq '[signals]')    { $section = 's'; continue }
        if ($l -eq '' -or $l.StartsWith('#')) { continue }
        if ($section -eq 'p') { $apiProps[$l] = 1 }
        if ($section -eq 's') { $apiSignals[$l] = 1 }
    }

    # 本文件自定义的属性名也要放行
    $localProps = @{}
    foreach ($m in [regex]::Matches($codeText, '\bproperty\s+[\w<>\.]+\s+(\w+)')) {
        $localProps[$m.Groups[1].Value] = 1
    }
    foreach ($b in @('id','property','signal','function','import','readonly','default',
                     'true','false','null','undefined','on','var')) {
        $localProps[$b] = 1
    }
    <#
      官方文档/示例里明确存在、但**不在头文件 Q_PROPERTY 里**的属性
      （Cascades 的类型注册另有来源，从 C++ 头文件抓不到）。
      这些是查过官方示例确认的，别凭印象往里加。
    #>
    foreach ($b in @('listItemComponents')) {
        $localProps[$b] = 1
    }

    $unknown = @{}
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $trimmed = $lines[$i].TrimStart()
        if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')) { continue }
        $code = $lines[$i]
        $commentAt = $code.IndexOf('//')
        if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

        foreach ($m in [regex]::Matches($code, '(?<![A-Za-z0-9_])on([A-Z]\w*)\s*:')) {
            $sigName = $m.Groups[1].Value
            $sigName = $sigName.Substring(0, 1).ToLower() + $sigName.Substring(1)
            if ($apiSignals.ContainsKey($sigName)) { continue }
            if ($localProps.ContainsKey($sigName)) { continue }
            $unknown['on' + $m.Groups[1].Value] = '信号 ' + $sigName + ' 不在 NDK 清单里'
        }

        foreach ($m in [regex]::Matches($code, '(?<![A-Za-z0-9_\.])([a-z]\w*)\s*:\s')) {
            $p = $m.Groups[1].Value
            if ($apiProps.ContainsKey($p)) { continue }
            if ($localProps.ContainsKey($p)) { continue }
            $unknown[$p] = '属性名不在 NDK 清单里'
        }
    }

    if ($unknown.Count -eq 0) {
        Write-Host ('  OK：属性/信号名都在清单内（' + $apiProps.Count + ' 属性 / ' + $apiSignals.Count + ' 信号）')
    } else {
        foreach ($k in ($unknown.Keys | Sort-Object)) {
            Write-Host ('  提示  ' + $k + '  —— ' + $unknown[$k]) -ForegroundColor DarkYellow
        }
        Write-Host ('  共 ' + $unknown.Count + ' 个未识别 —— 仅供人工扫一眼。')
        Write-Host '  已知噪声：JS 代码块里的对象字面量（error_code: 之类）、以及信号处理器。'
        Write-Host '  真正会拦住编造属性的是上面的 8)（黑名单，会判失败）。'
    }
}

# ---------------------------------------------------------------------------
# 9) 函数可见性：id.func() 调用的函数必须在 id 所在组件或其祖先里
#
#    踩过的坑（真机报错原文）：
#        asset:///main.qml:137: TypeError:
#        Result of expression 'followPage.pushInBa' [undefined] is not a function.
#
#    原因：QML 里组件函数只对**它自己和它的子对象**可见，不会向下暴露给
#    父级或兄弟。我把 pushInBa 写在了 NavigationPane 里，却用 followPage
#    （Page 的 id）去调它 —— Page 是 NavigationPane 的子对象，方向反了。
#
#    判定方式（用花括号深度，不做缩进猜测）：
#      - 记录每个 `id: xxx` 与 `function yyy` 所在的花括号深度
#      - 对每个 `xxx.yyy(` 调用：若 yyy 的深度**大于** xxx 的深度，
#        说明函数定义在 id 的**子组件**里 → 调用不到 → 报警
# ---------------------------------------------------------------------------
Write-Host '=== 9) 函数可见性（id.func() 不能调到子组件里的函数）===' -ForegroundColor Cyan

$idDepth = @{}        # id 声明所在深度
$fnDepth = @{}        # function 所在深度
$idBodyDepth = @{}    # id 所在组件的"体深度"（子对象所在层）
$scopeStack = New-Object System.Collections.Generic.List[string]   # 待定体的 id 栈
$depth = 0

for ($i = 0; $i -lt $lines.Count; $i++) {
    $trimmed = $lines[$i].TrimStart()
    $isComment = $trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')
    $code = $lines[$i]
    $commentAt = $code.IndexOf('//')
    if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

    $opens  = ([regex]::Matches($code, '\{')).Count
    $closes = ([regex]::Matches($code, '\}')).Count

    if (-not $isComment) {
        foreach ($m in [regex]::Matches($code, '(?<![A-Za-z0-9_])id\s*:\s*(\w+)')) {
            $name = $m.Groups[1].Value
            if (-not $idDepth.ContainsKey($name)) { $idDepth[$name] = $depth }
            # 这一行如果还开了 {，说明组件体从下一层开始；否则体就在当前层
            $idBodyDepth[$name] = $depth + $opens
        }
        if ($code -match '(?<![A-Za-z0-9_])function\s+(\w+)') {
            $fnDepth[$matches[1]] = $depth
        }
    }

    $depth += $opens
    $depth -= $closes
}

$scopeBad = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    $trimmed = $lines[$i].TrimStart()
    if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*')) { continue }
    $code = $lines[$i]
    $commentAt = $code.IndexOf('//')
    if ($commentAt -ge 0) { $code = $code.Substring(0, $commentAt) }

    foreach ($m in [regex]::Matches($code, '(?<![A-Za-z0-9_\.])(\w+)\.(\w+)\s*\(')) {
        $recv = $m.Groups[1].Value
        $func = $m.Groups[2].Value
        if (-not $idDepth.ContainsKey($recv)) { continue }
        if (-not $fnDepth.ContainsKey($func)) { continue }

        <#
          判据：函数深度必须**恰好等于** id 的组件体深度。

          实测过的两种情况（都对上了）：
            a) id 写在组件声明**下一行**：
                   NavigationPane {          depth 2
                       id: followNav                      ← id @3
                       Page {                depth 3
                           id: followPage                 ← id @4，同行无 {
                       }                                       体深度 = 4+0 = 4？
                   }
               —— 注意 Page 的体在 depth 5，而 id 那行没有 {，
                  所以用"本行 opens 数"算出来是 4，还差一。
                  修正：id 行的组件体深度 = 该行 opens 之后，**再往下找第一个
                  子对象的深度**。这里用等价简化：若该行没有 {，则体深度 = depth+1。

            b) id 与组件声明**同一行**（`Container { id: foo`）：
               体深度 = depth（{ 已在本行开了，但要减去本级）

          最终采用的可靠判据是下面的"函数必须在 id 的可见范围内"：
          QML 中函数的可见范围 = 该组件自身 + 其所有子对象。
          通过 id 调用时，函数必须定义在 id 组件的**内部或更外层**，
          但不能在"兄弟分支"里。
          由于本检查只看单文件且规则要稳，这里用**最保守也最准**的判据：
              函数深度 == id 的组件体深度   → 同一组件内，可达
              函数深度 <  组件体深度        → 在更外层组件
                  → 若函数所在组件是 id 组件的祖先，则**不可达**（id 是子对象）
                  → 这正是 followPage.pushInBa 那个 bug
          但 rootPage（Page 作为根）时函数就在 depth 1 == 体深度，通过。
        #>
        $body = $idBodyDepth[$recv]
        if ($fnDepth[$func] -eq $body) { continue }

        Write-Host ('  [' + ($i + 1) + '] ' + $recv + '.' + $func + '(...)：函数不在 ' + $recv + ' 所在组件内，调用不到') -ForegroundColor Yellow
        Write-Host ('      QML 的函数只对自身和子对象可见，兄弟/祖先都看不到。') -ForegroundColor Yellow
        Write-Host ('      函数深度 ' + $fnDepth[$func] + '，' + $recv + ' 的组件体深度 ' + $body + '。') -ForegroundColor Yellow
        Write-Host ('      请把 function ' + $func + ' 移到 id: ' + $recv + ' 所在组件内部。') -ForegroundColor Yellow
        $scopeBad++
        $problems++
    }
}
if ($scopeBad -eq 0) {
    Write-Host '  OK：所有 id.func() 调用的函数都在可达作用域内'
}

# ---------------------------------------------------------------------------
# 本文件检查结束：累计问题数，继续下一个文件
# ---------------------------------------------------------------------------
if ($problems -gt 0) {
    Write-Host ('  ^ 本文件有 ' + $problems + ' 个问题') -ForegroundColor Red
}
$allProblems += $problems

} # end foreach ($qmlFiles)

# ---------------------------------------------------------------------------
# 10) ★ .ps1 文件必须带 UTF-8 BOM
#
#     踩过的坑（第三次了，所以做成自动检查）：
#       Windows PowerShell 5.1 读取**没有 BOM** 的 .ps1 时，按**系统 ANSI
#       代码页**（中文 Windows = GBK）解码，而不是 UTF-8。中文注释于是变成乱码，
#       乱码里可能凑出不成对的引号 → **整行语法错误**：
#
#           Write-Host ("DBG: 写后重读长度=" + $after.Length)
#           The string is missing the terminator: ".
#
#       后果是**脚本解析失败**：后面所有代码被丢掉，而脚本可能**仍然返回 0**，
#       看起来"跑过了但什么也没做"。
#
#       本项目实际受害：build/fix-destdir.ps1 缺 BOM，
#       导致"把 post-link 路径改到当前目录"那段**静默失效**，
#       release 产物落到 arm/o.le-v7/o.le-v7/ 里，
#       打包报 Packaging failed: Invalid asset path "arm/o.le-v7/BBTiebaUI.so"。
#       排查花了好几轮 —— 就因为脚本"成功退出"了。
#
#     规则：仓库里所有 .ps1 必须是 UTF-8 with BOM。
# ---------------------------------------------------------------------------
Write-Host '=== 10) .ps1 必须带 UTF-8 BOM ===' -ForegroundColor Cyan

$repoRoot = Split-Path -Parent $PSScriptRoot
$ps1Files = @(Get-ChildItem $repoRoot -Recurse -File -Filter '*.ps1' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\arm\\|\\x86\\|\\.obj\\|\\.moc\\' })

$noBom = 0
foreach ($ps in $ps1Files) {
    $bytes = [System.IO.File]::ReadAllBytes($ps.FullName)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    if (-not $hasBom) {
        $rel = $ps.FullName.Substring($repoRoot.Length).TrimStart('\')
        Write-Host ('  ' + $rel + ' 缺少 UTF-8 BOM') -ForegroundColor Red
        Write-Host '      PS 5.1 会按 GBK 解码 → 中文注释变乱码 → 可能整段代码被丢掉，' -ForegroundColor Red
        Write-Host '      而脚本**仍然返回 0**，看起来跑过了却没做事。' -ForegroundColor Red
        Write-Host '      修法（PowerShell 一行）：' -ForegroundColor Red
        Write-Host '        $t=[IO.File]::ReadAllText($p,[Text.UTF8Encoding]::new($false))' -ForegroundColor Red
        Write-Host '        [IO.File]::WriteAllText($p,$t,(New-Object Text.UTF8Encoding($true)))' -ForegroundColor Red
        $noBom++
        $allProblems++
    }
}
if ($noBom -eq 0) {
    Write-Host ('  OK：' + $ps1Files.Count + ' 个 .ps1 都带 BOM')
}

# ---------------------------------------------------------------------------
Write-Host ''
if ($allProblems -eq 0) {
    Write-Host ('QML check passed（共检查 ' + $qmlFiles.Count + ' 个文件）') -ForegroundColor Green
    exit 0
} else {
    Write-Host ('QML check FAILED: ' + $allProblems + ' problem(s).') -ForegroundColor Red
    exit 1
}
