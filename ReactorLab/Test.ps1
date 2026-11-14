param(
    [string]$GameExe = (Join-Path $PSScriptRoot '../Runtime/Game.exe'),
    [switch]$Autoplay,
    [ValidateSet('Tests','CheckTimer','CheckDelegates','CheckTrigger','CheckAI','ReproTimerEvent','ReproTriggerEvent','ReproInstance')]
    [string]$Scene = 'Tests'
)
$ErrorActionPreference = 'Stop'
$scene = if ($Autoplay) { 'Autoplay' } else { $Scene }
$limit = if ($Autoplay) { 40 } else { 15 }
$logs = Join-Path $PSScriptRoot 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$out = Join-Path $logs "$scene.log"
$err = Join-Path $logs "$scene.err"
$standard = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Runtime/StandardResources'))
# Paths are quoted as individual native arguments, including project folders with spaces.
$nativeArgs = @('--project', ('"' + $PSScriptRoot + '"'), '--scene', "res://Scenes/$scene.level.json", '--resources', 'res', '--standard-resources', ('"' + $standard + '"'))
$process = Start-Process -FilePath $GameExe -ArgumentList $nativeArgs -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
$watch = [Diagnostics.Stopwatch]::StartNew()
$observed = $false
try {
    while ($watch.Elapsed.TotalSeconds -lt $limit -and -not $process.HasExited) {
        Start-Sleep -Milliseconds 150
        $text = Get-Content -LiteralPath $out -Raw -ErrorAction SilentlyContinue
        if (($Autoplay -and $text -match 'GAME WIN') -or (-not $Autoplay -and $text -match '(TEST|CHECK) COMPLETE')) {
            $observed = $true
            break
        }
    }
} finally {
    # Only the child started by this invocation is closed. Existing editor/game windows are untouched.
    if (-not $process.HasExited) {
        $null = $process.CloseMainWindow()
        if (-not $process.WaitForExit(3000)) { Stop-Process -Id $process.Id }
    }
}
$text = Get-Content -LiteralPath $out -Raw -ErrorAction SilentlyContinue
$errors = Get-Content -LiteralPath $err -Raw -ErrorAction SilentlyContinue
Write-Output $text
if ($errors) { Write-Output $errors }
$ok = $observed -and $text -notmatch '\bFAIL |\[Lua\].*(error|Error)' -and -not $errors
if (-not $Autoplay) { $ok = $ok -and $text -match '(TEST|CHECK) COMPLETE passes=\d+ failures=0' }
if (-not $ok) { Write-Error "Runtime verification failed or incomplete. See $logs"; exit 1 }
Write-Output 'Runtime verification passed.'
