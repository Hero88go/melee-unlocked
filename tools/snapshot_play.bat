@echo off
cd /d "%~dp0.."
if not exist build-review\port\Play mkdir build-review\port\Play
copy /Y build-review\port\Release\melee_port.exe build-review\port\Play\ >nul
copy /Y build-review\port\Release\*.dll build-review\port\Play\ >nul
echo Play snapshot updated from the Release build.
