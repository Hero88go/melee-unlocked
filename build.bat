@echo off
setlocal
rem One-command build from source: build.bat <path to your ACE-patched ISO>
rem Drop the ISO onto this file to do the same. Installs missing tools with winget when it can.
cd /d "%~dp0"
set ISO=%~1
if "%ISO%"=="" set ISO=%~dp0ace.iso
if not exist "%ISO%" (
  echo Usage: build.bat ^<path to your ACE-patched ISO^>   or drop the ISO onto build.bat
  pause
  exit /b 1
)

where git >nul 2>nul || (echo Installing Git... & winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements || goto :tools)
where python >nul 2>nul || (echo Installing Python... & winget install --id Python.Python.3.12 -e --accept-source-agreements --accept-package-agreements || goto :tools)
where cmake >nul 2>nul || (echo Installing CMake... & winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements || goto :tools)
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
  echo Installing Visual Studio 2022 Build Tools with the C++ workload ^(this takes a while, and runs silently^)...
  winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" || goto :tools
)

echo.
echo [1/6] Decomp source ^(the animation solver is generated from it^)
if not exist melee\src\sysdolphin\baselib\fobj.c (
  echo   cloning doldecomp/melee ^(source only, no game data^)
  git clone --depth 1 https://github.com/doldecomp/melee.git melee || goto :fail
)
echo [2/6] Extracting main.dol from the ISO
python tools\extract_dol.py "%ISO%" build\ace.dol --any || goto :fail
echo [3/6] Extracting the build's Gecko code table from the ISO
python tools\iso_file.py --iso "%ISO%" --extract codes.gct --out build\codes.gct || goto :nogct
echo [4/6] Translating the game and its code table to C++ ^(about 20 s^)
python port\recomp\recomp.py --dol build\ace.dol --modded-dol --no-slippi --mod-gct build\codes.gct --mod-gct-base 0x8065CC80 || goto :fail
echo [5/6] Configuring
cmake -S . -B build-ace -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON || goto :fail
echo [6/6] Compiling ^(20 to 40 minutes the first time^)
cmake --build build-ace --config Release --target melee_port --parallel || goto :fail
echo.
echo Done: build-ace\port\Release\melee_port.exe
echo Play with:  play.bat "%ISO%"
if not "%~1"=="" pause
exit /b 0

:nogct
echo.
echo There is no codes.gct on that disc, so it is not an m-ex build. This fork expects the
echo Smash ACE build; for vanilla Melee use the upstream project:
echo   https://github.com/Hero88go/melee-unlocked
echo To see what is on the disc:  python tools\iso_file.py --iso "%ISO%" --list
pause
exit /b 1

:tools
echo Could not install the tools automatically. Install Git, Python 3, CMake and Visual Studio
echo 2022 Build Tools ^(C++ desktop workload^), then run this file again.
pause
exit /b 1

:fail
echo Build failed; see the messages above.
pause
exit /b 1
