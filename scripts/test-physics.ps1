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
$sdk = Join-Path $buildRoot 'vcpkg_installed/x64-windows-static-md'
$arguments = @('/nologo','/std:c++20','/EHsc','/MDd','/Zi','/D_DEBUG','/DUNICODE','/D_UNICODE',
    "/I$projectRoot/Engine/include", "/I$buildRoot/_deps/directxtk-src/Inc",
    "/I$buildRoot/_deps/directxtex-src/DirectXTex", "/I$sdk/include/physx",
    "$projectRoot/tests/PhysicsChecks.cpp", "/Fo$testRoot/PhysicsChecks.obj",
    "/Fd$testRoot/PhysicsChecks.pdb", "/Fe$testRoot/PhysicsChecks.exe",
    "$buildRoot/Engine/Engine.lib", "$buildRoot/lib/DirectXTK.lib", "$buildRoot/lib/DirectXTex.lib")
$arguments += @(Get-ChildItem -LiteralPath "$sdk/debug/lib" -Filter '*.lib' | ForEach-Object { $_.FullName })
$arguments += @('/link','/DEBUG','user32.lib','gdi32.lib','shell32.lib','ole32.lib','oleaut32.lib','uuid.lib','comdlg32.lib','advapi32.lib')
& cl.exe @arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $testRoot 'PhysicsChecks.exe')
exit $LASTEXITCODE
