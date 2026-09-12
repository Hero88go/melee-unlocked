"""Capture a bounded offline replay with an isolated, muted Slippi playback copy.

Use an executable copied with its DLLs and Sys folder. No existing User folder
is read. Captures are evidence only after scene/frame/settings alignment.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import time
import uuid

from melee_iso import require_iso


def resize_owned_windows(pid, width, height, resized):
    """Resize hidden render windows without activating them or changing others."""
    user32 = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
    user32.SetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_long]
    user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]

    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid:
            return True
        current = wintypes.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(current))
        if current.right < 400 or current.bottom < 300:
            return True
        if hwnd in resized and current.right == width and current.bottom == height:
            return True
        # wx initially fits decorated windows to the desktop work area. Remove
        # decorations on this hidden trial so a full-height client isn't clipped.
        style = user32.GetWindowLongW(hwnd, -16)
        user32.SetWindowLongW(hwnd, -16, style & ~0x00CF0000)
        user32.SetWindowPos(hwnd, None, 0, 0, width, height,
                            0x0002 | 0x0004 | 0x0010 | 0x0020)
        resized.add(hwnd)
        return True

    user32.EnumWindows(visit, 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, required=True)
    ap.add_argument('--iso', type=Path)
    ap.add_argument('--replay', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--scale', type=int, choices=range(1, 7), default=1)
    ap.add_argument('--anisotropy', type=int, choices=(1, 2, 4, 8, 16), default=1)
    ap.add_argument('--width', type=int, default=640)
    ap.add_argument('--height', type=int, default=480)
    ap.add_argument('--end-frame', type=int, default=240)
    ap.add_argument('--timeout', type=float, default=90)
    args = ap.parse_args()
    args.iso = require_iso(args.iso)
    if args.width < 1 or args.height < 1 or args.timeout <= 0 or args.end_frame < -122:
        ap.error('invalid dimensions, timeout or end frame')
    # The render copy must be explicitly supplied; never discover the real install.
    if not (args.exe.parent / 'Sys').is_dir():
        ap.error('copy the playback executable, DLLs and Sys directory first')
    out = (args.out / uuid.uuid4().hex).resolve()
    config = out / 'User/Config'
    config.mkdir(parents=True)
    (config / 'Dolphin.ini').write_text(f'''[General]
DumpPath = {out / 'dump'}
[Interface]
ConfirmStop = False
UsePanicHandlers = False
[Core]
CPUThread = False
CPUCore = 1
GFXBackend = D3D
SlippiJukeboxEnabled = False
SlippiJukeboxVolume = 0
EmulationSpeed = 1.0
[DSP]
Backend = No audio output
Volume = 0
[Movie]
DumpFrames = True
DumpFramesSilent = True
[Display]
Fullscreen = False
RenderToMain = False
RenderWindowWidth = {args.width}
RenderWindowHeight = {args.height}
RenderWindowAutoSize = False
''')
    efb_scale = {1: 2, 2: 4, 3: 6, 4: 7, 5: 8, 6: 9}[args.scale]
    (config / 'GFX.ini').write_text(f'''[Settings]
AspectRatio = 4
EFBScale = {efb_scale}
MSAA = 1
SSAA = False
VSync = False
HiresTextures = False
DumpFramesAsImages = True
InternalResolutionFrameDumps = False
ShowFPS = False
[Enhancements]
MaxAnisotropy = {args.anisotropy.bit_length()-1}
UseScalingFilter = False
ForceFiltering = False
[Hacks]
EFBScaledCopy = True
''')
    comm = out / 'playback.json'
    # Start at the first recorded frame: skipping the countdown can alter visual
    # effects even when later fighter positions and the timer still match.
    comm.write_text(json.dumps(dict(mode='queue', queue=[dict(path=str(args.replay.resolve()),
        startFrame=-123, endFrame=args.end_frame)], commandId=out.name, outputOverlayFiles=False,
        isRealTimeMode=False, shouldResync=True, rollbackDisplayMethod='off')))
    cmd = [str(args.exe.resolve()), '-e', str(args.iso), '-b', '-u', str(config.parent),
           '-i', str(comm), '--hide-seekbar', '--cout']
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    ctypes.windll.user32.SetProcessDPIAware()
    complete = False
    with (out / 'stdout.log').open('w') as log, (out / 'stderr.log').open('w') as err:
        process = subprocess.Popen(cmd, cwd=args.exe.resolve().parent, stdout=log, stderr=err,
            startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        print(f'isolated muted Dolphin pid {process.pid}; output {out}', flush=True)
        deadline = time.monotonic() + args.timeout
        resized = set()
        try:
            while process.poll() is None and time.monotonic() < deadline:
                resize_owned_windows(process.pid, args.width, args.height, resized)
                text = (out / 'stdout.log').read_text(errors='replace')
                if '[CURRENT_FRAME]' in text and '[NO_GAME]' in text:
                    complete = True
                    break
                time.sleep(.1)
        finally:
            # Only this trial's child is terminated; the real installation is untouched.
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
    images = sorted((out / 'dump/Frames').glob('*.png'))
    dimensions = {}
    for image in images:
        with image.open('rb') as stream:
            header = stream.read(24)
        if len(header) != 24 or header[:8] != b'\x89PNG\r\n\x1a\n':
            continue
        w, h = struct.unpack('>II', header[16:24])
        key = f'{w}x{h}'
        dimensions[key] = dimensions.get(key, 0) + 1
    result = dict(complete=complete, command=cmd, images=len(images),
        exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        replay_sha256=hashlib.sha256(args.replay.read_bytes()).hexdigest(),
        scale=args.scale, requested_output=[args.width, args.height], image_dimensions=dimensions,
        anisotropy=args.anisotropy,
        aa='none', temporal_reconstruction=False, start_frame=-123, end_frame=args.end_frame)
    (out / 'capture.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result), flush=True)
    return 0 if complete and dimensions.get(f'{args.width}x{args.height}', 0) > 1 else 1


if __name__ == '__main__':
    raise SystemExit(main())
