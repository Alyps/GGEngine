param([string]$BuildDirectory = 'out/build/x64-debug')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { Write-Error 'Visual Studio C++ tools were not found.'; exit 1 }
& (Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Push-Location $projectRoot
try {
    & cmake --preset x64-debug -B $BuildDirectory '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=' -DFETCHCONTENT_FULLY_DISCONNECTED=OFF
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build $BuildDirectory --parallel 8
    exit $LASTEXITCODE
} finally { Pop-Location }
