"""Package the locally compiled experiment without editing the stock baseline."""
from pathlib import Path
import shutil
import json
import hashlib

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'slippi/Binary/x64'
target = ROOT / 'runtime/prototype'
baseline = ROOT / 'runtime/slippi'
target.mkdir(parents=True, exist_ok=True)
for entry in source.iterdir():
    dst = target / entry.name
    if entry.is_dir():
        shutil.copytree(entry, dst, dirs_exist_ok=True)
    else:
        shutil.copy2(entry, dst)
for name in ('Config', 'GC'):
    # Preserve project settings and memory cards on subsequent builds.
    if not (target / 'User' / name).exists():
        shutil.copytree(baseline / 'User' / name, target / 'User' / name)
for ini in (target / 'User/Config').glob('*.ini'):
    text = ini.read_text(encoding='utf-8-sig')
    text = text.replace(str(baseline), str(target)).replace(baseline.as_posix(), target.as_posix())
    if ini.name == 'Dolphin.ini':
        text = text.replace('RenderWindowWidth = 780', 'RenderWindowWidth = 640')
        text = text.replace('RenderWindowHeight = 850', 'RenderWindowHeight = 528')
        text = text.replace('ConfirmStop = True', 'ConfirmStop = False')
    if ini.name == 'GFX.ini':
        text = text.replace('HiresTextures = True', 'HiresTextures = False')
    ini.write_text(text, encoding='utf-8')
(target / 'portable.txt').touch()
exe = target / 'Slippi Dolphin.exe'
report = {'executable': str(exe), 'sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
          'status': 'Experimental image interpolation; fidelity, latency and multiplayer unverified.'}
(ROOT / 'reports/prototype.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
