@echo off
setlocal
set "GAME=%~dp0..\Runtime\Game.exe"
if not "%~1"=="" set "GAME=%~1"
"%GAME%" --project "%~dp0." --scene res://Scenes/Main.level.json --resources res --standard-resources "%~dp0..\Runtime\StandardResources"
if errorlevel 1 pause
