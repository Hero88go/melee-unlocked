@echo off
cd /d "%~dp0"
python tools\launch_native.py --threaded-renderer --fps unlocked --frame-mode authored --fullscreen --volume 70 %*
