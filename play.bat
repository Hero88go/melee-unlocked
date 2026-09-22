@echo off
setlocal
rem Drop your ACE-patched ISO onto this file. Builds the port first if it has not been built yet.
cd /d "%~dp0"
set ISO=%~1
if "%ISO%"=="" set ISO=%~dp0ace.iso
if not exist "%ISO%" (
  echo Drop your Smash ACE ISO onto play.bat, or put it next to it named ace.iso
  echo.
  echo This needs the ACE build patch applied to your own clean Melee NTSC 1.02 ISO.
  echo The patch and its instructions are at
  echo   https://github.com/Chri222k/ACE-BUILD-PUBLIC-/releases
  echo Nothing from the game or from the ACE build is included here.
  pause
  exit /b 1
)
if not exist build-ace\port\Release\melee_port.exe (
  echo No build yet; building from source first.
  call build.bat "%ISO%" || exit /b 1
)
build-ace\port\Release\melee_port.exe --iso "%ISO%" --threaded-renderer --fps unlocked --frame-mode authored --scale auto --volume 70
if errorlevel 1 pause
