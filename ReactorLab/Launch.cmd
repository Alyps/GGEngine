@echo off
setlocal
set "BUILD=%~dp0..\out\build"
set "GAME=%~1"
if defined GAME goto check
set "GAME=%BUILD%\x64-qa-debug\Game\Game.exe"
if exist "%GAME%" goto check
set "GAME=%BUILD%\x64-release\Game\Game.exe"
if exist "%GAME%" goto check
set "GAME=%BUILD%\x64-debug\Game\Game.exe"

:check
if not exist "%GAME%" (
    echo Game.exe missing. Build Game from the QA branch or pass its path to Launch.cmd. 1>&2
    exit /b 1
)
"%GAME%" --project "%~dp0." --scene res://Scenes/Main.level.json --resources res --standard-resources "%~dp0..\Engine\StandartResources"
exit /b %errorlevel%
 