@echo off
setlocal
echo This starts the isolated STOCK Slippi baseline. Unlocked FPS is not implemented yet.
if not exist "%~dp0reports\baseline.json" (
  echo Run python tools\prepare_runtime.py successfully first.
  exit /b 1
)
pushd "%~dp0runtime\slippi"
start "" "Slippi Dolphin.exe" -e "C:\Games\Smash\DOLPHIN AND SMASH GAMES\Super Smash Bros. Melee (v1.02).iso"
popd
