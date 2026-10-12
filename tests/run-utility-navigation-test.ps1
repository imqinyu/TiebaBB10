# Dedicated Qt 4 declarative regression; no shared backend MOC or build files.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$qtRuntime = 'C:\QtSDK\Desktop\Qt\4.8.1\mingw'
$compilerDir = 'C:\QtSDK\mingw\bin'
$env:PATH = "$compilerDir;$qtRuntime\bin;$env:PATH"
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force tests\build,tests\tmp | Out-Null
    & "$compilerDir\g++.exe" "-I$qtRuntime\include" "-I$qtRuntime\include\QtCore" "-I$qtRuntime\include\QtDeclarative" tests\test_utility_navigation.cpp "-L$qtRuntime\lib" -lQtDeclarative4 -lQtCore4 -lQtGui4 -o tests\build\test_utility_navigation.exe
    if ($LASTEXITCODE -ne 0) { throw 'Utility navigation build failed' }
    cmd /c 'tests\build\test_utility_navigation.exe > tests\tmp\utility-navigation-test.log 2>&1'
    $testExit = $LASTEXITCODE
    Get-Content tests\tmp\utility-navigation-test.log
    if ($testExit -ne 0) { throw "Utility navigation test failed: $testExit" }
} finally { Pop-Location }
