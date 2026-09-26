"""Capture a settings appearance over an actual scripted match, not the boot menu."""
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import time
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--iso',required=True,type=Path)
    ap.add_argument('--out',required=True,type=Path)
    ap.add_argument('--style',required=True,type=int,choices=range(5))
    args=ap.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    ini=out/'settings.ini'
    ini.write_text(f'startup 1\noverlaystyle {args.style}\nbackend d3d12\n'
                   'vsync 0\nfps 60\nwindow 800x600\n')
    ppm=out/'match.ppm'
    logpath=out/'stdout.txt'
    env=os.environ.copy();env['MELEE_UI_ICON_VARIANT']='B'
    command=[str(ROOT/'build-integration/port/Release/melee_port.exe'),
        '--iso',str(args.iso),'--hidden','--load-settings','--pc-settings',
        '--settings-path',str(ini),'--backend','d3d12','--window','800x600',
        '--script',str(ROOT/'port/scripts/graphics_yoshi.txt'),
        '--frames','2750','--capture-sim-frame','2660','--capture',str(ppm),
        '--volume','0','--no-music']
    user=ctypes.WinDLL('user32',use_last_error=True)
    callback_type=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.EnumWindows.argtypes=[callback_type,wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    with logpath.open('w') as log:
        game=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,
            stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            deadline=time.monotonic()+130
            while time.monotonic()<deadline:
                if game.poll() is not None:raise RuntimeError('game exited before F1')
                if '[frame 2580]' in logpath.read_text(errors='replace'):break
                time.sleep(.2)
            else:raise TimeoutError('script did not reach match frame 2580')
            found=[]
            @callback_type
            def visit(hwnd,param):
                pid=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
                name=ctypes.create_unicode_buffer(128);user.GetClassNameW(hwnd,name,128)
                if pid.value==game.pid and name.value=='MeleePortWindow':found.append(hwnd)
                return True
            user.EnumWindows(visit,0)
            if not found:raise RuntimeError('game window missing at match frame 2580')
            if not user.PostMessageW(found[0],0x100,0x70,0):raise ctypes.WinError(ctypes.get_last_error())
            time.sleep(.25)
            user.PostMessageW(found[0],0x101,0x70,0xC0000000)
            game.wait(timeout=35)
        finally:
            if game.poll() is None:game.kill();game.wait()
    if game.returncode:raise RuntimeError(f'game exited {game.returncode}')
    if not ppm.is_file():raise RuntimeError('match capture missing')
    with Image.open(ppm) as image:image.save(out/'match.png')
    print(out/'match.png')

if __name__=='__main__':main()
