#requires -Version 2.0
<#
    Host-side verification script (development only; NOT part of the BB10 project).

    Purpose: build and run the tests under tests/ with desktop Qt 4.8 + mingw,
    so the pure logic layer (MD5 / JSON / URL encoding / signing / session /
    models / parsers) can be verified without a BB10 device.

    Two traps that are already handled here -- do not reintroduce them:
      1. C:\QtSDK\mingw\bin MUST be on PATH. gcc's cc1plus child process needs the
         DLLs in that directory; without them it exits silently with
         0xC0000135 (STATUS_DLL_NOT_FOUND) and prints nothing at all.
      2. Every header containing Q_OBJECT must go through moc first, otherwise the
         link fails with "undefined reference to vtable for ...", which looks like
         a coding bug but is really just missing meta-object code.

    Usage (from the BBTieba directory):
        powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
        powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 -Only test_api

    NOTE: this file is saved as UTF-8 with BOM on purpose. Windows PowerShell 5.1
    reads .ps1 files as ANSI (GBK on zh-CN) when there is no BOM, which corrupts
    any non-ASCII text and can break parsing.
#>

param(
    [string]$Only = ''
)

$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $PSScriptRoot
$qt     = 'C:\QtSDK\Desktop\Qt\4.8.1\mingw'
$mingw  = 'C:\QtSDK\mingw\bin'
$moc    = Join-Path $qt 'bin\moc.exe'
$build  = Join-Path $PSScriptRoot 'build'
$tmp    = Join-Path $PSScriptRoot 'tmp'
$mocOut = Join-Path $build 'moc'

foreach ($p in @($qt, $mingw, $moc)) {
    if (-not (Test-Path $p)) {
        throw ("Toolchain component not found: " + $p + " -- edit `$qt / `$mingw in this script.")
    }
}

New-Item -ItemType Directory -Force -Path $build, $tmp, $mocOut | Out-Null
$env:PATH = "$mingw;$qt\bin;$env:PATH"

$srcRoot  = Join-Path $root 'src'
$tiebaDir = Join-Path $srcRoot 'tieba'
$includes = @(
    # stubs 放最前面：用它顶掉真正的 bb/cascades/* 与 bb/system/*
    # （开发机上没有这些真头文件，TiebaController 又必须能编译）
    "-I$PSScriptRoot\stubs",
    "-I$qt\include",
    "-I$qt\include\QtCore",
    "-I$qt\include\QtNetwork",
    "-I$tiebaDir",
    # 测试里会 #include "TiebaController.hpp"（在 src 根下）
    "-I$srcRoot"
)

# ---------------------------------------------------------------- backend sources
$sources = @(
    'util\TbMd5.cpp',
    'util\TbJson.cpp',
    'util\TbString.cpp',
    'util\TbImageCache.cpp',
    'net\TbCookieStore.cpp',
    'net\TbSigner.cpp',
    'net\TbHttpClient.cpp',
    'session\TbSession.cpp',
    'model\TbItems.cpp',
    'model\TbUserDataCache.cpp',
    'util\TbThreadNavigation.cpp',
    'model\TbParsers.cpp',
    'model\TbPcParsers.cpp',
    'model\TbThreadLibrary.cpp',
    'api\TbSearchService.cpp',
    'api\TbFavoriteService.cpp',
    'api\TbSignService.cpp',
    'api\TbApi.cpp'
) | ForEach-Object { Join-Path $tiebaDir $_ } | Where-Object { Test-Path $_ }

# ------------------------------------------------- controller（测试要调它的方法）
# TiebaController 不在 src\tieba 下，单独加；它依赖 tests\stubs 里的
# bb/cascades/* 与 bb/system/* 替身（开发机上没有这些真头文件）。
$sources += (Join-Path $srcRoot 'TiebaController.cpp')

# ------------------------------------------------- moc: headers that use Q_OBJECT
$headersWithQObject = @()
$headersWithQObject += Get-ChildItem $tiebaDir -Recurse -Filter '*.hpp' |
    Where-Object { (Get-Content $_.FullName -Raw) -match 'Q_OBJECT' }
# TiebaController.hpp 在 src 根下（不在 src\tieba），测试要链接它的实现，
# 所以它的 moc 也得生成，否则会 "undefined reference to vtable"。
$controllerHeader = Join-Path $srcRoot 'TiebaController.hpp'
if ((Test-Path $controllerHeader) -and
    ((Get-Content $controllerHeader -Raw) -match 'Q_OBJECT')) {
    $headersWithQObject += Get-Item $controllerHeader
}

$mocSources = @()
$mocNames = @()
$mocErrorLog = Join-Path $tmp 'moc.log'
foreach ($h in $headersWithQObject) {
    $out = Join-Path $mocOut ('moc_' + $h.BaseName + '.cpp')
    # Run moc through cmd.exe on purpose: Qt tools print
    # "Untested Windows version 6.2 detected!" on stderr, and PowerShell 5.1
    # turns native-command stderr into a terminating error under
    # $ErrorActionPreference='Stop' -- even with a 2> redirect. Going through
    # cmd.exe keeps stderr out of PowerShell's error stream entirely.
    cmd /c "`"$moc`" `"$($h.FullName)`" -o `"$out`" 2> `"$mocErrorLog`""
    if ($LASTEXITCODE -ne 0) {
        $details = ''
        if (Test-Path $mocErrorLog) { $details = (Get-Content $mocErrorLog -Raw) }
        throw ("moc failed for " + $h.FullName + ":" + $details)
    }
    $mocSources += $out
    $mocNames += $h.BaseName
}

Write-Host ("Backend sources: " + $sources.Count + ", moc sources: " + $mocSources.Count) -ForegroundColor DarkGray
Write-Host ("moc: " + ($mocNames -join ', ')) -ForegroundColor DarkGray

# ---------------------------------------------------------------------------
# Compile-only check for TiebaController.
#
# It lives in src/ rather than src/tieba/ and does not depend on Cascades, so it
# can be compiled here even though ApplicationUI (which does pull in Cascades)
# cannot. This catches signature drift between the controller and the backend
# without needing a device.
# ---------------------------------------------------------------------------
$controllerSrc = Join-Path $srcRoot 'TiebaController.cpp'
if (Test-Path $controllerSrc) {
    Write-Host "### Compile check: TiebaController.cpp" -ForegroundColor Cyan
    $cLog = Join-Path $tmp 'TiebaController.build.log'
    # -I<stubs> 放在最前面：用它顶掉真正的 bb/cascades/*（开发机上没有 Cascades）
    $stubs = Join-Path $PSScriptRoot 'stubs'
    & "$mingw\g++.exe" -c -Wall "-I$stubs" @includes -I$srcRoot `
        -o (Join-Path $build 'TiebaController.o') $controllerSrc 2>&1 |
        Tee-Object -FilePath $cLog | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "### COMPILE FAILED: TiebaController.cpp" -ForegroundColor Red
        Get-Content $cLog |
            Where-Object { $_ -match ': (error|warning)' } |
            Select-Object -First 40 |
            ForEach-Object { Write-Host ("    " + $_) }
        exit 1
    }
    Write-Host "### OK: TiebaController.cpp" -ForegroundColor Green
}

# ---------------------------------------------------------------- test targets
$tests = @(
    @{ Name = 'test_md5';  Src = Join-Path $PSScriptRoot 'test_md5.cpp' },
    @{ Name = 'test_core'; Src = Join-Path $PSScriptRoot 'test_core.cpp' },
    @{ Name = 'test_api';  Src = Join-Path $PSScriptRoot 'test_api.cpp' },
    @{ Name = 'test_imagecache'; Src = Join-Path $PSScriptRoot 'test_imagecache.cpp' },
    @{ Name = 'test_expand'; Src = Join-Path $PSScriptRoot 'test_expand.cpp' },
    @{ Name = 'test_forumcache'; Src = Join-Path $PSScriptRoot 'test_forumcache.cpp' },
    @{ Name = 'test_settings'; Src = Join-Path $PSScriptRoot 'test_settings.cpp' },
    @{ Name = 'test_accounts'; Src = Join-Path $PSScriptRoot 'test_accounts.cpp' }
    @{ Name = 'test_ui'; Src = Join-Path $PSScriptRoot 'test_ui.cpp' }
    @{ Name = 'test_userpage_api'; Src = Join-Path $PSScriptRoot 'test_userpage_api.cpp' }
    @{ Name = 'test_search_api'; Src = Join-Path $PSScriptRoot 'test_search_api.cpp' }
    @{ Name = 'test_favorites'; Src = Join-Path $PSScriptRoot 'test_favorites.cpp' }
    @{ Name = 'test_login_protocol'; Src = Join-Path $PSScriptRoot 'test_login_protocol.cpp' }
    @{ Name = 'test_video_threads'; Src = Join-Path $PSScriptRoot 'test_video_threads.cpp' }
    @{ Name = 'test_thread_navigation'; Src = Join-Path $PSScriptRoot 'test_thread_navigation.cpp' }
    @{ Name = 'test_userdata_cache'; Src = Join-Path $PSScriptRoot 'test_userdata_cache.cpp' }
    @{ Name = 'test_controller_finish'; Src = Join-Path $PSScriptRoot 'test_controller_finish.cpp' }
    @{ Name = 'test_thread_likes'; Src = Join-Path $PSScriptRoot 'test_thread_likes.cpp' }
    @{ Name = 'test_search_service'; Src = Join-Path $PSScriptRoot 'test_search_service.cpp' }
    @{ Name = 'test_sign_service'; Src = Join-Path $PSScriptRoot 'test_sign_service.cpp' }
    @{ Name = 'test_sign_lifecycle'; Src = Join-Path $PSScriptRoot 'test_sign_lifecycle.cpp' }
    @{ Name = 'test_thread_library'; Src = Join-Path $PSScriptRoot 'test_thread_library.cpp' }
)

$failed = 0
$ran = 0
foreach ($t in $tests) {
    if ($Only -ne '' -and $t.Name -ne $Only) { continue }
    if (-not (Test-Path $t.Src)) {
        Write-Host ("Skip " + $t.Name + " (source missing)") -ForegroundColor Yellow
        continue
    }

    $exe = Join-Path $build ($t.Name + '.exe')
    Write-Host ""
    Write-Host ("### Build " + $t.Name) -ForegroundColor Cyan

    $log = Join-Path $tmp ($t.Name + '.build.log')
    & "$mingw\g++.exe" -o $exe @includes $t.Src @sources @mocSources "-L$qt\lib" -lQtCore4 -lQtNetwork4 2>&1 |
        Tee-Object -FilePath $log | Out-Null
    $buildExit = $LASTEXITCODE

    if ($buildExit -ne 0) {
        Write-Host ("### BUILD FAILED: " + $t.Name) -ForegroundColor Red
        Get-Content $log |
            Where-Object { $_ -match 'error|undefined reference' } |
            Select-Object -First 40 |
            ForEach-Object { Write-Host ("    " + $_) }
        $failed++
        continue
    }

    Write-Host ("### Run " + $t.Name) -ForegroundColor Cyan
    $ran++

    # Same reason as the moc step: Qt prints an "Untested Windows version"
    # warning on stderr, so run through cmd.exe, merge stderr into a log, and
    # let the console show the test output only.
    $runLog = Join-Path $tmp ($t.Name + '.run.log')
    cmd /c "`"$exe`" 2> `"$runLog`""
    $runExit = $LASTEXITCODE

    if (Test-Path $runLog) {
        $warnings = Get-Content $runLog | Where-Object { $_ -notmatch 'Untested Windows version' }
        foreach ($w in $warnings) { Write-Host ("    [stderr] " + $w) -ForegroundColor DarkYellow }
    }

    if ($runExit -ne 0) {
        Write-Host ("### TEST FAILED: " + $t.Name + " (exit=" + $runExit + ")") -ForegroundColor Red
        $failed++
    } else {
        Write-Host ("### PASS: " + $t.Name) -ForegroundColor Green
    }
}

if ($failed -gt 0) {
    Write-Host ""
    Write-Host ("$failed / $ran test targets failed") -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# QML 静态检查
#
# 放在最后但同样重要：Cascades 的 QML 是 Qt Quick 的子集，写错一个类型
# （比如用了 Repeater）会让整个 QML 加载失败 → 黑屏，而且只有真机日志
# 才看得到。这个检查把这类错误提前到开发机。
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host '### C++ safety check' -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'check-cpp-safety.ps1')
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "C++ safety check failed" -ForegroundColor Red
    $failed++
}

Write-Host '### QML check' -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'check-qml.ps1')
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "QML check failed" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# assets 索引是否与目录内容一致
#
# 为什么需要：.assets.index 只有 IDE 改 assets 时才会更新，make **不会**重生成。
# 新加了图标但索引还是旧的 —— 这时容易怀疑"资源没打包"，
# 与其靠猜，不如每次回归都对一遍。用 tools\regen-assets-index.ps1 的 -WhatIfOnly 干跑。
# ---------------------------------------------------------------------------
Write-Host '### assets index check' -ForegroundColor Cyan
$regenScript = Join-Path (Split-Path $PSScriptRoot -Parent) 'tools\regen-assets-index.ps1'
if (Test-Path $regenScript) {
    $idxOut = & powershell -NoProfile -ExecutionPolicy Bypass -File $regenScript -WhatIfOnly 2>&1
    $drift = $idxOut | Select-String -Pattern '^新增 |^移除 '
    if ($drift) {
        Write-Host '  FAIL：assets/.assets.index 与目录内容不一致' -ForegroundColor Red
        $idxOut | Where-Object { $_ -match '^\s*[+-] ' } | ForEach-Object { Write-Host ("  " + $_) }
        Write-Host '  修法：powershell -NoProfile -ExecutionPolicy Bypass -File tools\regen-assets-index.ps1' -ForegroundColor Yellow
        $failed++
    } else {
        Write-Host '  OK：assets 索引与目录一致'
    }
} else {
    Write-Host '  SKIP：没有 tools\regen-assets-index.ps1' -ForegroundColor DarkGray
}

Write-Host ""
if ($failed -gt 0) {
    Write-Host ("FAILED: $failed / $ran test targets failed") -ForegroundColor Red
    exit 1
}
Write-Host ("All passed ($ran test targets)") -ForegroundColor Green
exit 0
