"""Check that moving the mouse over the 02 menu highlights a nonselected row."""
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import time

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', required=True)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--category', type=int, default=6, choices=range(7))
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    width, height = 800, 600
    panel_w = max(390, min(560, width * 0.38))
    panel_x = width - panel_w
    row_h = min(61.0, (height - 210.0) / 7.0)
    row_top = 161.0 + args.category * (row_h - 1.0)
    # Sample inside the right side of the sheared card, clear of its label.
    x = round(panel_x + 19.0 + (panel_w - 39.0) * 0.78)
    tilt = (panel_w - 39.0) * 0.10
    y = round(row_top + row_h - tilt * 0.78 - 7.0)

    ini = out / 'settings.ini'
    ini.write_text('startup 1\noverlaystyle 0\noverlaypalette0 0\nvsync 0\nfps 60\nwindow 800x600\n')
    env = os.environ.copy()
    env.pop('MELEE_SETTINGS_TAB', None)
    env.pop('MELEE_TEST_SETTINGS_TAB', None)
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    user32.GetForegroundWindow.restype = wintypes.HWND
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.EnumWindows.argtypes = [ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM), wintypes.LPARAM]
    user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
    user32.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    old_window = user32.GetForegroundWindow()
    old_cursor = wintypes.POINT()
    user32.GetCursorPos(ctypes.byref(old_cursor))
    capture = out / 'hover.ppm'
    with (out / 'stdout.txt').open('w') as log:
        proc = subprocess.Popen([
            str(ROOT / 'build-integration/port/Release/melee_port.exe'),
            '--iso', args.iso, '--load-settings', '--pc-settings-open',
            '--settings-path', str(ini), '--window', '800x600', '--backend', 'd3d12',
            '--frames', '850', '--capture-frame', '0', '--capture-sim-frame', '650',
            '--capture', str(capture), '--volume', '0', '--no-music',
        ], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            deadline = time.monotonic() + 40
            hwnd = None
            while time.monotonic() < deadline:
                windows = []
                @callback_type
                def visit(window, _):
                    pid = wintypes.DWORD()
                    user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
                    if pid.value == proc.pid and user32.IsWindowVisible(window):
                        windows.append(window)
                    return True
                user32.EnumWindows(visit, 0)
                if windows:
                    hwnd = windows[0]
                    break
                if proc.poll() is not None:
                    raise RuntimeError('game exited before its window appeared')
                time.sleep(.15)
            if hwnd is None:
                raise RuntimeError('game window did not appear')
            user32.SetForegroundWindow(hwnd)
            time.sleep(.25)
            point = wintypes.POINT(x, y)
            user32.ClientToScreen(hwnd, ctypes.byref(point))
            user32.SetCursorPos(point.x, point.y)
            time.sleep(.5)
            actual = wintypes.POINT()
            user32.GetCursorPos(ctypes.byref(actual))
            input_mode = 'real cursor'
            if (actual.x, actual.y) != (point.x, point.y) or user32.GetForegroundWindow() != hwnd:
                # A posted move still exercises ImGui's window-message input path when this
                # automated process cannot claim foreground focus. It does not click or change
                # any setting; the output pixel below confirms whether hover reached the renderer.
                location = ((y & 0xffff) << 16) | (x & 0xffff)
                if not user32.PostMessageW(hwnd, 0x0200, 0, location):
                    raise RuntimeError('Windows denied cursor and posted mouse-move input')
                input_mode = 'posted WM_MOUSEMOVE'
            proc.wait(timeout=40)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            user32.SetCursorPos(old_cursor.x, old_cursor.y)
            if old_window:
                user32.SetForegroundWindow(old_window)

    if proc.returncode:
        raise RuntimeError(f'game returned {proc.returncode}')
    if not capture.is_file():
        raise RuntimeError('hover frame capture was not produced')
    with Image.open(capture) as frame:
        rgb = frame.convert('RGB')
        pixel = rgb.getpixel((x, y))
    if not (pixel[0] > 130 and pixel[1] < 120 and pixel[2] > 70):
        raise RuntimeError(f'category {args.category} did not show the pink hover face at {(x, y)}: {pixel}')
    print(f'PASS: category {args.category} hover rendered pink at {(x, y)} via {input_mode}: {pixel}')


if __name__ == '__main__':
    main()
