#!/usr/bin/env python3
"""M11: audit a scratch release package the way a player receives it. Nothing is published.

Extracts the Stable Recomp Legacy zip into a fresh folder, then checks: the file list (no Markdown
or handoff files, Legacy default launch, both engines and the symbol file present), a hidden, muted
boot of every packaged game executable from that folder with the packaged Sys data, that the game
DLL's .gnu_debuglink names the packaged .dbg with a matching CRC and that addresses resolve through
it, and a byte scan of every file in both zips for credential patterns.

    python tools/m11_package_audit.py --package-dir run-source/m11-scratch-package-YYYYMMDD
"""
import argparse, hashlib, json, re, shutil, subprocess, zipfile, zlib
from pathlib import Path

ISO = r'C:/Games/Smash/DOLPHIN AND SMASH GAMES/Super Smash Bros. Melee (v1.02).iso'
TOOLS = Path(r'C:/Users/Chandler/toolchains/winlibs-gcc-15.3/mingw64/bin')
PATTERNS = {
    'apikey_prefix': rb'apikey_[0-9a-f]{8}', 'typesafe_env': rb'TYPESAFE_API_KEY',
    'bearer_token': rb'Bearer [A-Za-z0-9._-]{20,}', 'github_token': rb'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_',
    'openai_style': rb'sk-[A-Za-z0-9]{32,}', 'aws_key': rb'AKIA[0-9A-Z]{16}',
    'private_key': rb'-----BEGIN [A-Z ]*PRIVATE KEY', 'webmail_address': rb'[A-Za-z0-9._%+-]{3,}@(gmail|outlook|hotmail|yahoo)\.com',
    'password_assign': rb'(?i)password\s*=\s*\S{4,}', 'slippi_account': rb'"connectCode"|"playKey"',
    'discord_token': rb'[MN][A-Za-z\d]{23}\.[\w-]{6}\.[\w-]{27}',
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--package-dir', type=Path, required=True)
    ap.add_argument('--extra-pattern', action='append', default=[], help='additional literal to scan for (not echoed)')
    a = ap.parse_args()
    zips = sorted(a.package_dir.glob('*.zip'))
    stable = next(z for z in zips if 'Stable' in z.name)
    report = {'zips': {z.name: hashlib.sha256(z.read_bytes()).hexdigest() for z in zips}}

    clean = a.package_dir / 'clean-install'
    shutil.rmtree(clean, ignore_errors=True)
    with zipfile.ZipFile(stable) as z:
        names = z.namelist()
        z.extractall(clean)
    root = clean / names[0].split('/')[0]
    files = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    bat = (root / 'MeleeUnlocked.bat').read_text(encoding='utf-8', errors='replace')
    report['contents'] = dict(
        files=len(files), markdown=[f for f in files if f.lower().endswith('.md')],
        has=dict((n, n in files) for n in ('melee_port.exe', 'melee_port_compat.exe', 'melee_source.exe',
                                          'melee_game.dll', 'melee_game.dbg', 'MeleeUnlockedLauncher.exe')),
        bat_default_engine='melee_port.exe' if re.search(r'(?m)^melee_port\.exe ', bat) else 'unexpected')

    boots = {}
    for exe in ('melee_port.exe', 'melee_port_compat.exe', 'melee_source.exe'):
        tag = exe[:-4]
        log = (clean / f'boot-{tag}.log').resolve()   # the game runs from inside the install folder
        cmd = [str(root / exe), '--iso', ISO, '--hidden', '--volume', '0', '--fast', '--frames', '1200',
               '--sys-dir', 'Sys', '--user-dir', f'User/Slippi-{tag}', '--replay-dir', 'Replays',
               '--card-dir', f'User/GC/CardA-{tag}', '--settings-path', f'settings-{tag}.ini', '--log-file', str(log)]
        rc = subprocess.run(cmd, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=400,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).returncode
        text = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
        boots[exe] = dict(exit=rc, complete='exit requested after 1200' in text,
                          fatal=bool(re.search(r'game crash|game panic|game stopped|FATAL', text)))
    report['boots'] = boots

    dll, dbg = root / 'melee_game.dll', root / 'melee_game.dbg'
    link = subprocess.run([str(TOOLS / 'objdump.exe'), '-s', '-j', '.gnu_debuglink', str(dll)],
                          capture_output=True, text=True).stdout
    hexwords = ''.join(re.findall(r'^ [0-9a-f]+ ((?:[0-9a-f]{2,8} ?){1,4})', link, re.M)).replace(' ', '')
    raw = bytes.fromhex(hexwords)
    name = raw.split(b'\0')[0].decode()
    crc_stored = int.from_bytes(raw[-4:], 'little')
    crc_actual = zlib.crc32(dbg.read_bytes()) & 0xFFFFFFFF
    exports = subprocess.run([str(TOOLS / 'objdump.exe'), '-p', str(dll)], capture_output=True, text=True).stdout
    rva = int(re.search(r'\[\s*0\] \+base\[\s*\d+\]\s+([0-9a-f]+)', exports).group(1), 16)
    resolved = subprocess.run([str(TOOLS / 'addr2line.exe'), '-f', '-e', str(dbg), hex(0x82800000 + rva)],
                              capture_output=True, text=True).stdout.split()
    report['symbols'] = dict(debuglink=name, crc_stored=f'{crc_stored:08x}', crc_actual=f'{crc_actual:08x}',
                             crc_match=crc_stored == crc_actual, export_rva=hex(rva),
                             resolves_to=resolved[0] if resolved else None)

    scan = {}
    extra = [re.escape(p.encode()) for p in a.extra_pattern]
    for z in zips:
        with zipfile.ZipFile(z) as zf:
            for n in zf.namelist():
                if n.endswith('/'):
                    continue
                data = zf.read(n)
                for k, p in list(PATTERNS.items()) + [(f'extra_{i}', p) for i, p in enumerate(extra)]:
                    if re.search(p, data):
                        scan.setdefault(z.name, []).append((n, k))
    report['credential_scan'] = dict(hits=scan, total=sum(len(v) for v in scan.values()))

    ok = (not report['contents']['markdown'] and all(report['contents']['has'].values())
          and report['contents']['bat_default_engine'] == 'melee_port.exe'
          and all(b['exit'] == 0 and b['complete'] and not b['fatal'] for b in boots.values())
          and report['symbols']['crc_match'] and report['symbols']['resolves_to'] == 'mu_game_entry'
          and report['credential_scan']['total'] == 0)
    report['passed'] = ok
    (a.package_dir / 'audit.json').write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
