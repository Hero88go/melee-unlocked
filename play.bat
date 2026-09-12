@echo off
setlocal
rem Drop your Melee NTSC 1.02 ISO onto this file. Builds the port first if it has not been built yet.
cd /d "%~dp0"
set ISO=%~1
if "%ISO%"=="" set ISO=%~dp0melee.iso
if not exist "%ISO%" (
  echo Drop your Melee NTSC 1.02 ISO onto play.bat, or put it next to it named melee.iso
  pause
  exit /b 1
)
if not exist build-review\port\Release\melee_port.exe (
  echo No build yet; building from source first.
  call build.bat "%ISO%" || exit /b 1
)
build-review\port\Release\melee_port.exe --iso "%ISO%" --threaded-renderer --fps unlocked --frame-mode authored --scale auto --volume 70
if errorlevel 1 pause
