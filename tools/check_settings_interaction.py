"""Windows message-driven UI smoke check, using an isolated preference file."""
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iso', required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--style', type=int, default=2)
    ap.add_argument('--palette', type=int, choices=range(4),
                    help='click a GD Melee palette swatch instead of the Video VSync switch')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    ini = out/'settings.ini'
    ini.write_text('startup 1\noverlaystyle %d\noverlaypalette2 0\nvsync 0\nfps 60\nwindow 800x600\n' % args.style)
    env = os.environ.copy()
    env['MELEE_SETTINGS_TAB'] = 'Customize' if args.palette is not None else 'Video'
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes=[wintypes.HWND]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.ShowWindow.argtypes=[wintypes.HWND,ctypes.c_int]
    user.SetForegroundWindow.argtypes=[wintypes.HWND]
    user.ClientToScreen.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.POINT)]
    user.GetForegroundWindow.restype=wintypes.HWND
    user.WindowFromPoint.argtypes=[wintypes.POINT]
    user.WindowFromPoint.restype=wintypes.HWND
    previous_window=user.GetForegroundWindow()
    previous_cursor=wintypes.POINT()
    user.GetCursorPos(ctypes.byref(previous_cursor))
    with (out/'stdout.txt').open('w') as log:
        p = subprocess.Popen([str(ROOT/'build-integration/port/Release/melee_port.exe'),
            '--iso', args.iso, '--load-settings', '--pc-settings-open',
            '--settings-path',str(ini),'--window','800x600','--backend','d3d12',
            '--frames','850','--capture-frame','0','--capture-sim-frame','650',
            '--capture',str(out/'after.ppm'),'--volume','0','--no-music'],
            cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+40
            hwnd=None
            while time.monotonic()<deadline:
                found=[]
                @callback_type
                def visit(w,param):
                    pid=wintypes.DWORD()
                    user.GetWindowThreadProcessId(w,ctypes.byref(pid))
                    if pid.value==p.pid and user.IsWindowVisible(w):found.append(w)
                    return True
                user.EnumWindows(visit,0)
                if found:
                    hwnd=found[0];break
                if p.poll() is not None:raise RuntimeError('game exited before interaction')
                time.sleep(.15)
            if not hwnd:raise RuntimeError('game window did not become ready')
            user.ShowWindow(hwnd,5)
            user.SetForegroundWindow(hwnd)
            time.sleep(.3)
            # GD's palette swatches are visible without scrolling at 800x600.
            if args.palette is not None:
                if args.style != 2: raise RuntimeError('palette click probe is scoped to GD Melee')
                x,y=(170+160*args.palette,215)
            else:
                # VSync is the third kit row; the other themes use labeled switch rows.
                x,y={0:(750,315),1:(737,289),2:(550,210),3:(725,238),4:(738,289)}[args.style]
            cursor=wintypes.POINT(x,y)
            user.ClientToScreen(hwnd,ctypes.byref(cursor))
            user.SetCursorPos(cursor.x,cursor.y)
            time.sleep(.3)
            actual=wintypes.POINT()
            user.GetCursorPos(ctypes.byref(actual))
            (out/'input-debug.txt').write_text(
                'target=%d,%d actual=%d,%d hwnd=%s foreground=%s cursor_window=%s\n'%(
                    cursor.x,cursor.y,actual.x,actual.y,hwnd,user.GetForegroundWindow(),
                    user.WindowFromPoint(actual)))
            if (actual.x, actual.y) != (cursor.x, cursor.y) or user.GetForegroundWindow() != hwnd:
                raise RuntimeError('Windows did not grant cursor or foreground focus; interaction probe is inconclusive')
            if args.palette is not None:
                # Real pointer events avoid a queued WM_MOUSELEAVE invalidating ImGui's
                # mouse position between posted move and button messages.
                user.mouse_event(0x0001,1,0,0,0)
                time.sleep(.2)
                user.mouse_event(0x0002,0,0,0,0)
                time.sleep(.2)
                user.mouse_event(0x0004,0,0,0,0)
            else:
                location=(y<<16)|x
                for msg,wparam in ((0x200,0),(0x201,1),(0x202,0)):
                    if not user.PostMessageW(hwnd,msg,wparam,location):raise ctypes.WinError(ctypes.get_last_error())
                    time.sleep(.15)
            p.wait(timeout=40)
        finally:
            if p.poll() is None:p.kill();p.wait()
            user.SetCursorPos(previous_cursor.x,previous_cursor.y)
            if previous_window:user.SetForegroundWindow(previous_window)
    saved=ini.read_text()
    if args.palette is not None:
        if 'overlaypalette2 %d\n'%args.palette not in saved:
            raise RuntimeError('GD palette click did not persist')
    elif 'vsync 1\n' not in saved:raise RuntimeError('VSync click did not persist')
    if 'overlaystyle %d\n'%args.style not in saved:raise RuntimeError('appearance did not persist')
    if p.returncode:raise RuntimeError('game returned %s'%p.returncode)
    if args.palette is not None:
        print('PASS: GD palette %d clicked and persisted; game exited normally'%args.palette)
    else:
        print('PASS: style %d VSync switch changed and persisted; game exited normally'%args.style)


if __name__=='__main__':main()
