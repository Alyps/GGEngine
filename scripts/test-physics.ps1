param([string]$BuildDirectory = 'out/build/x64-physx-debug')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { Write-Error 'Visual Studio C++ tools were not found.'; exit 1 }
& (Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$buildRoot = Join-Path $projectRoot $BuildDirectory
$testRoot = Join-Path $buildRoot 'physics-checks'
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
# Agents.md разрешает только сборку: проверяем API без запуска симуляции.
$arguments = @('/nologo','/c','/std:c++20','/EHsc','/MDd','/D_DEBUG','/DUNICODE','/D_UNICODE',
    "/I$projectRoot/Engine/include", "/I$buildRoot/_deps/directxtk-src/Inc",
    "/I$buildRoot/_deps/directxtex-src/DirectXTex", "$projectRoot/tests/PhysicsChecks.cpp",
    "/Fo$testRoot/PhysicsChecks.obj")
& cl.exe @arguments
$compileExit = $LASTEXITCODE
if ($compileExit -eq 0) { Write-Output 'Physics public API compilation passed; no executable was run.' }
exit $compileExit
