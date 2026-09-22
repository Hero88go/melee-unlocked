@echo off
setlocal
title Smash ACE Unlocked
cd /d "%~dp0"
set ISO=%~1
if "%ISO%"=="" set ISO=%~dp0ace.iso
if not exist "%ISO%" (
  echo.
  echo   Smash ACE Unlocked
  echo   ==================
  echo.
  echo   Drag your ACE-patched ISO onto this file,
  echo   or put it in this folder named  ace.iso  and run this again.
  echo.
  echo   You need the Smash ACE build patch applied to your own clean
  echo   Melee NTSC 1.02 ISO first. The patch and its instructions are at
  echo     https://github.com/Chri222k/ACE-BUILD-PUBLIC-/releases
  echo.
  pause
  exit /b 1
)

echo.
echo   Setting up Smash ACE Unlocked. The first run builds the game on
echo   this PC; after that it starts in seconds.
echo.

where git >nul 2>nul || (
  echo   Installing Git...
  winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements || goto :tools
  set "PATH=%PATH%;%ProgramFiles%\Git\cmd"
)

if not exist melee-unlocked\.git (
  echo   Downloading Smash ACE Unlocked...
  git clone --branch ace-build --depth 1 https://github.com/sennecaelen/melee-unlocked.git melee-unlocked || goto :fail
) else (
  echo   Updating Smash ACE Unlocked...
  pushd melee-unlocked
  git pull --ff-only
  popd
)

pushd melee-unlocked
if not exist build-ace\port\Release\melee_port.exe (
  call build.bat "%ISO%" || (popd & goto :fail)
)
call play.bat "%ISO%"
popd
exit /b 0

:tools
echo.
echo   Could not install Git automatically. Install it from https://git-scm.com
echo   and run this file again.
pause
exit /b 1

:fail
echo.
echo   Setup failed; the messages above say where.
pause
exit /b 1
