@echo off
cd /d "%~dp0"
python tools\launch_native.py --threaded-renderer --fps unlocked --frame-mode authored --window 1920x1440 --volume 70 %*
