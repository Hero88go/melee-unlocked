"""Verify the existing backup, isolate copied paths, and validate the vanilla ISO.

Never writes to the source installation or ISO. Does not launch an emulator.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path.home() / 'AppData/Roaming/Slippi Launcher/netplay'
BACKUP = ROOT / 'backups/slippi-netplay'
RUNTIME = ROOT / 'runtime/slippi'
ISO = Path('C:/Games/Smash/DOLPHIN AND SMASH GAMES/Super Smash Bros. Melee (v1.02).iso')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    report_dir = ROOT / 'reports'
    report_dir.mkdir(exist_ok=True)
    files = []
    for src in sorted(SOURCE.rglob('*')):
        if not src.is_file():
            continue
        relative = src.relative_to(SOURCE)
        dst = BACKUP / relative
        source_hash = digest(src)
        if not dst.is_file() or digest(dst) != source_hash:
            raise RuntimeError('Backup differs: ' + str(relative))
        files.append({'path': relative.as_posix(), 'sha256': source_hash,
                      'bytes': src.stat().st_size})
    if not files:
        raise RuntimeError('No source installation files found')
    (report_dir / 'backup-manifest.json').write_text(json.dumps(files, indent=2))
    # Preserve all preferences, but redirect references back into the isolated copy.
    for ini in (RUNTIME / 'User/Config').glob('*.ini'):
        text = ini.read_text(encoding='utf-8-sig')
        for old, new in [(str(SOURCE), str(RUNTIME)),
                         (SOURCE.as_posix(), RUNTIME.as_posix())]:
            text = text.replace(old, new)
        # Replay output otherwise defaults to the user's shared Documents/Slippi folder.
        if ini.name == 'Dolphin.ini':
            lines = text.splitlines()
            section = ''
            found = False
            for i, line in enumerate(lines):
                if line.startswith('['):
                    section = line.strip()
                if section == '[Core]' and line.split('=', 1)[0].strip() == 'SlippiReplayDir':
                    lines[i] = 'SlippiReplayDir = ' + (ROOT / 'runtime/replays').as_posix()
                    found = True
            if not found:
                i = lines.index('[Core]')
                lines.insert(i + 1, 'SlippiReplayDir = ' + (ROOT / 'runtime/replays').as_posix())
            text = '\n'.join(lines) + '\n'
        ini.write_text(text, encoding='utf-8')
    (ROOT / 'runtime/replays').mkdir(exist_ok=True)
    (RUNTIME / 'portable.txt').touch()
    with ISO.open('rb') as f:
        header = f.read(0x440)
        if header[:6] != b'GALE01' or header[7] != 2:
            raise RuntimeError('Expected GALE01 revision 2 ISO')
        dol_offset = struct.unpack_from('>I', header, 0x420)[0]
        f.seek(dol_offset)
        dol_header = f.read(0x100)
        offsets = struct.unpack_from('>18I', dol_header, 0)
        sizes = struct.unpack_from('>18I', dol_header, 0x90)
        length = max(o + s for o, s in zip(offsets, sizes))
        if length > 16 * 1024 * 1024:
            raise RuntimeError('Invalid DOL length')
        f.seek(dol_offset)
        dol = f.read(length)
    dol_sha1 = hashlib.sha1(dol).hexdigest()
    if dol_sha1 != '08e0bf20134dfcb260699671004527b2d6bb1a45':
        raise RuntimeError('DOL does not match vanilla Melee 1.02: ' + dol_sha1)
    extracted = ROOT / 'melee/orig/GALE01/sys/main.dol'
    extracted.write_bytes(dol)
    result = {'source': str(SOURCE), 'backup': str(BACKUP), 'runtime': str(RUNTIME),
              'backup_files_verified': len(files), 'iso': str(ISO),
              'dol_sha1': dol_sha1, 'iso_sha256': digest(ISO),
              'status': 'Isolated stock baseline. No unlocked rendering implemented.'}
    (report_dir / 'baseline.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
