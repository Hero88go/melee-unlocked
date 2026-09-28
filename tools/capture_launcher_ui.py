"""Capture the current launcher window and optionally select a sidebar tab."""
import argparse
import ctypes
import time
from pathlib import Path
from PIL import Image, ImageGrab

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()
user32.GetDlgItem.restype = ctypes.c_void_p
user32.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_void_p]
user32.GetClassNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
user32.EnumChildWindows.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
user32.FindWindowExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p]
user32.FindWindowExW.restype = ctypes.c_void_p
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202

class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

parser = argparse.ArgumentParser()
parser.add_argument("pid", type=int)
parser.add_argument("output")
parser.add_argument("--tab", type=int, choices=range(5))
parser.add_argument("--lobby-view", choices=("profile", "friends", "history"))
parser.add_argument("--screen", action="store_true")
parser.add_argument("--emoji", action="store_true")
parser.add_argument("--theme", action="store_true")
args = parser.parse_args()

found = []
CALLBACK = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

@CALLBACK
def collect(hwnd, _):
    pid = ctypes.c_ulong()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    name = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(hwnd, name, len(name))
    if pid.value == args.pid and name.value == "MeleeUnlockedLauncher":
        found.append(hwnd)
    return True

user32.EnumWindows(collect, 0)
if not found:
    raise RuntimeError(f"No visible launcher window for PID {args.pid}")
window = found[0]
user32.ShowWindow(window, 9)
user32.SetWindowPos(window, 0, 30, 30, 0, 0, 0x0001 | 0x0004 | 0x0010)
user32.SetForegroundWindow(window)
if args.tab is not None:
    x, y = 112, 152 + args.tab * 38 + 17
    point = (y << 16) | x
    user32.SendMessageW(window, WM_LBUTTONDOWN, 1, point)
    user32.SendMessageW(window, WM_LBUTTONUP, 0, point)
    time.sleep(1.5)
if args.lobby_view or args.emoji:
    lobby = user32.FindWindowExW(window, None, "MeleeUnlockedLobby", None)
    if not lobby:
        raise RuntimeError("Lobby window not found")
    if args.lobby_view:
        control_id = {"friends": 530, "history": 531, "profile": 532}[args.lobby_view]
        child = user32.GetDlgItem(lobby, control_id)
        user32.SendMessageW(lobby, 0x0111, control_id, child)
    if args.emoji:
        user32.SendMessageW(lobby, 0x0111, 547, user32.GetDlgItem(lobby, 547))
    time.sleep(.5)
if args.theme:
    user32.SendMessageW(window, 0x0111, 109, user32.GetDlgItem(window, 109))
    time.sleep(.5)
rect = RECT()
user32.GetWindowRect(window, ctypes.byref(rect))
time.sleep(.3)
path = Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
width, height = rect.right - rect.left, rect.bottom - rect.top

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", ctypes.c_ulong), ("biWidth", ctypes.c_long),
                ("biHeight", ctypes.c_long), ("biPlanes", ctypes.c_ushort),
                ("biBitCount", ctypes.c_ushort), ("biCompression", ctypes.c_ulong),
                ("biSizeImage", ctypes.c_ulong), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", ctypes.c_ulong),
                ("biClrImportant", ctypes.c_ulong)]

gdi32 = ctypes.windll.gdi32
user32.GetDC.restype = ctypes.c_void_p
gdi32.CreateCompatibleDC.restype = ctypes.c_void_p
gdi32.CreateDIBSection.restype = ctypes.c_void_p
gdi32.SelectObject.restype = ctypes.c_void_p
gdi32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]
gdi32.CreateDIBSection.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint,
                                    ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p, ctypes.c_ulong]
gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
gdi32.DeleteDC.argtypes = [ctypes.c_void_p]
user32.PrintWindow.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
desktop_dc = user32.GetDC(0)
memory_dc = gdi32.CreateCompatibleDC(desktop_dc)
header = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
bits = ctypes.c_void_p()
bitmap = gdi32.CreateDIBSection(desktop_dc, ctypes.byref(header), 0, ctypes.byref(bits), 0, 0)
old_bitmap = gdi32.SelectObject(memory_dc, bitmap)
printed = user32.PrintWindow(window, memory_dc, 2)
if printed and not args.screen:
    pixels = ctypes.string_at(bits, width * height * 4)
    Image.frombuffer("RGB", (width, height), pixels, "raw", "BGRX", 0, 1).save(path)
else:
    ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom), all_screens=True).save(path)
gdi32.SelectObject(memory_dc, old_bitmap)
gdi32.DeleteObject(bitmap)
gdi32.DeleteDC(memory_dc)
user32.ReleaseDC(0, desktop_dc)
print(path.resolve())
