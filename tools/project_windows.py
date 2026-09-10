"""Inspect/close only windows belonging to the last project launch."""
import argparse
import ctypes
from ctypes import wintypes
import json
import re
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--close', action='store_true')
parser.add_argument('--capture', action='store_true')
parser.add_argument('--tas', action='store_true')
parser.add_argument('--button')
parser.add_argument('--screenshot', action='store_true')
parser.add_argument('--state', choices=['save', 'load'])
parser.add_argument('--list-controls', action='store_true')
parser.add_argument('--control-id', type=int)
parser.add_argument('--value')
args = parser.parse_args()
pid = json.loads((root / 'reports/last-launch.json').read_text())['pid']
kernel = ctypes.windll.kernel32
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
process = kernel.OpenProcess(0x1000, False, pid)
if not process:
    raise SystemExit('The recorded project process has exited.')
try:
    executable = ctypes.create_unicode_buffer(32768)
    length = wintypes.DWORD(len(executable))
    if not kernel.QueryFullProcessImageNameW(process, 0, executable, ctypes.byref(length)):
        raise SystemExit('Cannot verify process executable; no windows touched.')
    if Path(executable.value).resolve() != (root / 'runtime/prototype/Slippi Dolphin.exe').resolve():
        raise SystemExit('PID belongs to a different executable; no windows touched.')
finally:
    kernel.CloseHandle(process)
user = ctypes.windll.user32
user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user.SendMessageW.restype = wintypes.LPARAM
user.GetParent.argtypes = [wintypes.HWND]
user.GetParent.restype = wintypes.HWND
user.SetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
windows = []


def visit(handle, unused):
    owner = wintypes.DWORD()
    user.GetWindowThreadProcessId(handle, ctypes.byref(owner))
    if owner.value == pid:
        title = ctypes.create_unicode_buffer(512)
        user.GetWindowTextW(handle, title, 512)
        if title.value:
            windows.append({'handle': handle, 'title': title.value, 'visible': bool(user.IsWindowVisible(handle))})
        if args.close and re.fullmatch(r'Faster Melee - Slippi \([^|]+\)', title.value):
            user.PostMessageW(handle, 0x10, 0, 0)
    return True


user.OpenInputDesktop.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
user.OpenInputDesktop.restype = wintypes.HANDLE
user.EnumDesktopWindows.argtypes = [wintypes.HANDLE, callback_type, wintypes.LPARAM]
user.CloseDesktop.argtypes = [wintypes.HANDLE]
user.SetThreadDesktop.argtypes = [wintypes.HANDLE]
desktop = user.OpenInputDesktop(0, False, 0x1ff)
# A tool-launched test may live on the automation desktop rather than the user's.
# Enumerate both; executable/PID verification still scopes every operation.
user.EnumWindows(callback_type(visit), 0)
if desktop:
    if not user.SetThreadDesktop(desktop):
        raise SystemExit('Could not attach this helper thread to the input desktop.')
    user.EnumDesktopWindows(desktop, callback_type(visit), 0)
windows = list({w['handle']: w for w in windows}.values())
print(json.dumps([w for w in windows if w['visible']], indent=2))
if args.tas or args.screenshot or args.state:
    source = (root / 'slippi/Source/Core/DolphinWX/Globals.h').read_text()
    identifiers = {}
    value = 0
    for name, number in re.findall(r'^\s*(IDM_[A-Z0-9_]+)(?:\s*=\s*(\d+))?,', source, re.MULTILINE):
        value = int(number) if number else value + 1
        identifiers[name] = value
        if name == 'IDM_SCREENSHOT':
            break
    main = next(w for w in windows if re.fullmatch(r'Faster Melee - Slippi \([^|]+\)', w['title']))
    command = identifiers['IDM_TAS_INPUT' if args.tas else 'IDM_SCREENSHOT']
    if args.state:
        command = identifiers['IDM_SAVE_SLOT_1' if args.state == 'save' else 'IDM_LOAD_SLOT_1']
    user.PostMessageW(main['handle'], 0x111, command, 0)
    print('Sent project menu command', command)
if args.button or args.list_controls or args.control_id is not None:
    dialog = next(w for w in windows if w['title'] == 'TAS Input - GameCube Controller 1')
    controls = []
    def child_visit(handle, unused):
        title = ctypes.create_unicode_buffer(512)
        klass = ctypes.create_unicode_buffer(128)
        user.GetWindowTextW(handle, title, 512)
        user.GetClassNameW(handle, klass, 128)
        controls.append({'handle': handle, 'text': title.value, 'class': klass.value,
                         'id': user.GetDlgCtrlID(handle)})
        return True
    user.EnumChildWindows(dialog['handle'], callback_type(child_visit), 0)
    if args.list_controls:
        print(json.dumps(controls, indent=2))
    if args.control_id is not None:
        control = next(c for c in controls if c['id'] == args.control_id)
        if control['class'] == 'msctls_trackbar32':
            user.SendMessageW(control['handle'], 0x405, 1, int(args.value))
            user.SendMessageW(user.GetParent(control['handle']), 0x115, 5, control['handle'])
        else:
            user.SetWindowTextW(control['handle'], args.value)
            user.SendMessageW(user.GetParent(control['handle']), 0x111,
                              (0x0300 << 16) | (control['id'] & 0xffff), control['handle'])
    if args.button:
        button = next(c for c in controls if c['class'] == 'Button' and c['text'] == args.button)
        user.SendMessageW(button['handle'], 0xF5, 0, 0)  # BM_CLICK emits the wx checkbox event
        time.sleep(0.15)
        user.SendMessageW(button['handle'], 0xF5, 0, 0)
        print('Tapped project TAS button', args.button)
if not args.close:
    (root / 'reports/windows.json').write_text(json.dumps(windows, indent=2))
if args.capture:
    from PIL import Image
    match = next((w for w in windows if w['title'].startswith('Melee')), None)
    if not match:
        raise SystemExit('No project presentation window found.')
    handle = match['handle']
    rect = wintypes.RECT()
    user.GetWindowRect(handle, ctypes.byref(rect))
    width, height = rect.right-rect.left, rect.bottom-rect.top
    class BitmapInfo(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('width', wintypes.LONG), ('height', wintypes.LONG),
                    ('planes', wintypes.WORD), ('bits', wintypes.WORD), ('compression', wintypes.DWORD),
                    ('image_size', wintypes.DWORD), ('xppm', wintypes.LONG), ('yppm', wintypes.LONG),
                    ('colors', wintypes.DWORD), ('important', wintypes.DWORD)]
    gdi = ctypes.windll.gdi32
    gdi.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi.CreateCompatibleDC.restype = wintypes.HDC
    gdi.CreateDIBSection.argtypes = [wintypes.HDC, ctypes.c_void_p, wintypes.UINT,
                                   ctypes.POINTER(ctypes.c_void_p), wintypes.HANDLE, wintypes.DWORD]
    gdi.CreateDIBSection.restype = wintypes.HANDLE
    gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
    gdi.SelectObject.restype = wintypes.HANDLE
    gdi.DeleteObject.argtypes = [wintypes.HANDLE]
    gdi.DeleteDC.argtypes = [wintypes.HDC]
    user.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
    info = BitmapInfo(ctypes.sizeof(BitmapInfo), width, -height, 1, 32, 0, width*height*4, 0,0,0,0)
    dc = gdi.CreateCompatibleDC(None)
    bits = ctypes.c_void_p()
    bitmap = gdi.CreateDIBSection(dc, ctypes.byref(info), 0, ctypes.byref(bits), None, 0)
    previous = gdi.SelectObject(dc, bitmap)
    try:
        if not user.PrintWindow(handle, dc, 2):
            raise SystemExit('PrintWindow failed')
        pixels = ctypes.string_at(bits, width*height*4)
        Image.frombytes('RGB', (width,height), pixels, 'raw', 'BGRX').save(root / 'reports/presentation-window.png')
        print('Saved reports/presentation-window.png')
    finally:
        gdi.SelectObject(dc, previous)
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(dc)
