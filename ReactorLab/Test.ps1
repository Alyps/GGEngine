param(
    [string]$GameExe,
    [switch]$Autoplay,
    [ValidateSet('Tests','CheckTimer','CheckDelegates','CheckTrigger','CheckAI','ReproTimerEvent','ReproTriggerEvent','ReproInstance')]
    [string]$Scene = 'Tests'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $GameExe) {
    foreach ($build in 'x64-qa-debug', 'x64-release', 'x64-debug') {
        $candidate = Join-Path $repo "out/build/$build/Game/Game.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $GameExe = $candidate
            break
        }
    }
}
if (-not $GameExe -or -not (Test-Path -LiteralPath $GameExe -PathType Leaf)) {
    throw 'Game.exe missing. Build Game from the QA branch or pass its path with -GameExe.'
}
$GameExe = (Resolve-Path -LiteralPath $GameExe).Path
if ($Autoplay -and $PSBoundParameters.ContainsKey('Scene')) {
    throw 'Specify either -Autoplay or -Scene.'
}
$sceneName = if ($Autoplay) { 'Autoplay' } else { $Scene }
$limit = if ($Autoplay) { 40 } else { 15 }
$run = '{0}-{1}-{2}' -f $sceneName, (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $PID
$logs = Join-Path $repo "out/verification/ReactorLab/$run"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$out = Join-Path $logs 'stdout.log'
$err = Join-Path $logs 'stderr.log'
$standard = Join-Path $repo 'Engine/StandartResources'

$complete = if ($Autoplay) { '(?m)^\[Lua\] GAME WIN\r?$' } else { '(?m)^\[Lua\] (TEST|CHECK) COMPLETE passes=[1-9]\d* failures=0\r?$' }
$terminal = if ($Autoplay) { '(?m)^\[Lua\] GAME (WIN|LOST)\r?$' } else { '(?m)^\[Lua\] (TEST|CHECK) COMPLETE .*\r?$' }
$failure = '(?m)^\[UtilityAI\] |^\[Lua\] (?:FAIL |.+\.lua:|Invalid script path|(?:Native delegate|Delegate)[^:]*:)'

# Start-Process joins its argument list on Windows, so quote paths with spaces.
$nativeArgs = @('--project', ('"' + $PSScriptRoot + '"'), '--scene', "res://Scenes/$sceneName.level.json", '--resources', 'res', '--standard-resources', ('"' + $standard + '"'))
Write-Output "Testing $sceneName with $GameExe"
# Share the caller's console while leaving the game window available for WM_CLOSE.
$process = Start-Process -FilePath $GameExe -ArgumentList $nativeArgs -WorkingDirectory $PSScriptRoot -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
$watch = [Diagnostics.Stopwatch]::StartNew()
$timedOut = $false
$forced = $false
try {
    while ($true) {
        $text = Get-Content -LiteralPath $out -Raw -Encoding UTF8
        if ($process.HasExited -or $text -match $terminal -or $text -match $failure) { break }
        if ($watch.Elapsed.TotalSeconds -ge $limit) {
            $timedOut = $true
            break
        }
        Start-Sleep -Milliseconds 150
    }
} finally {
    # Request ordinary window shutdown so the game's destructors run.
    if (-not $process.HasExited) {
        $null = $process.CloseMainWindow()
        if (-not $process.WaitForExit(3000)) {
            $forced = $true
            $process.Kill()
            $process.WaitForExit()
        }
    }
}
$text = Get-Content -LiteralPath $out -Raw -Encoding UTF8
$errors = Get-Content -LiteralPath $err -Raw -Encoding UTF8
Write-Output $text
if ($errors) { Write-Output $errors }
$exitCode = $process.ExitCode
$process.Dispose()
if ($timedOut -or $forced -or $exitCode -ne 0 -or $text -notmatch $complete -or $text -match $failure -or $errors) {
    throw "Verification failed for $sceneName (exit=$exitCode, timeout=$timedOut, forced shutdown=$forced). Logs: $logs"
}
Write-Output "Verification passed for $sceneName (exit=$exitCode). Logs: $logs"
