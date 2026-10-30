#!/usr/bin/env python3
"""Package the Linux release: release/MeleeUnlocked-<version>-linux-x86_64.tar.gz.

The same game files as the Windows zip (tools/package_release.py: Sys, SysPlayback, the CRT shader
pack, the settings appearance assets, translations, licenses), with the Linux Source Port, its game
library (with melee_game.dbg and melee_game.snapexcl), the Linux launcher and, in lib/, every
library they need that differs between distributions, so the archive runs on any x86-64 desktop
without installing anything. The archive's root folder is MeleeUnlocked-<version>, which is what the
updater unpacks and copies over the installed folder.

Run inside Linux (WSL is fine) after tools/build_linux.py:
    python3 tools/package_linux_release.py
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
EXE = 'melee_' + 'source'

# Libraries the system must provide itself: the C runtime and the loader, the graphics driver stack,
# the display server's client libraries, the sound server's, and udev (from AppImage's exclude list).
SYSTEM_LIBRARIES = (
    'linux-vdso', 'ld-linux', 'libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so', 'libresolv.so',
    'libutil.so', 'libanl.so', 'libnsl.so', 'libBrokenLocale', 'libmvec.so',
    'libGL.so', 'libGLX', 'libGLdispatch', 'libEGL', 'libOpenGL', 'libGLESv', 'libdrm', 'libgbm', 'libglapi',
    'libX11', 'libxcb', 'libXau', 'libXdmcp', 'libXext', 'libXrender', 'libXrandr', 'libXcursor', 'libXi',
    'libXinerama', 'libXss', 'libXxf86vm', 'libXfixes', 'libxkbcommon', 'libwayland', 'libdecor',
    'libasound', 'libpulse', 'libpipewire', 'libjack', 'libudev', 'libsystemd', 'libdbus', 'libuuid',
    'libfontconfig', 'libfreetype', 'libexpat', 'libz.so', 'libgcc_s.so',
)

README = """Melee Unlocked {version} for Linux

Melee Unlocked runs Super Smash Bros. Melee natively on Linux: the same Source Port as on Windows,
with Slippi online, replays and Watch live, spectating, mods, skins, music packs, Gecko codes, the
settings panel, and GameCube adapters, box controllers and other pads. You need your own clean,
uncompressed Melee NTSC 1.02 disc image; none is included.

Experimental: this is the first Linux version. If something does not work, Help > Send logs in
the launcher sends us your log and a note. The Static Recomp engine (needed for Melee 3D) is
Windows only for now.

Start

  ./MeleeUnlocked

Choose your disc image on the Play page, then Play. Settings opens the full settings panel, which
is also in game (F1). Replays lists your replays and the Slippi Launcher's; Watch live plays the
broadcast you open in the Slippi Launcher's Spectate tab. Online is the Melee Unlocked lobby, shared
with Windows players on the same version.

Requirements

  - An x86-64 Linux desktop with a processor that has AVX2 (Intel Haswell 2013 or newer, AMD Ryzen or
    newer) and an OpenGL 4.5 graphics driver (Mesa or NVIDIA).
  - Everything else the game needs is in lib/ beside it.
  - zenity or kdialog for file pickers (most desktops have one).

GameCube adapter, box controllers, PlayStation and Switch Pro pads

  Give your desktop user access once, then unplug and reconnect the device:

    sudo install -m 644 51-melee-unlocked-gc-adapter.rules /etc/udev/rules.d/
    sudo udevadm control --reload-rules

  Xbox-style and other pads work without it.

Music packs

  F1 > Audio > turn on Use custom music > Open music packs. Tracks.txt there lists the original
  track names. For /audio/name.hps, put your song at MusicPacks/audio/name.mp3 (or .wav, .m4a,
  .flac, .ogg, or stereo .hps). For random picks, put several songs in MusicPacks/audio/name/; one
  folder named name+othername serves both tracks, such as a stage's main and alternate songs. A
  direct replacement comes before a playlist. Extract ZIP packs first: audio goes directly inside
  MusicPacks. Leave and re-enter the stage or menu to hear a new song. Packs replace music, not
  voices or sound effects.

Updates

  The launcher and the game's settings panel offer each new version and install it in place.

Saves, settings and logs

  Everything stays in this folder: User/ (memory card, Slippi sign-in), port-settings.ini,
  Replays/, melee_port.log. Help > Send logs sends the log to the developer with your note.

Licensing

  Melee Unlocked is free software under the GNU General Public License version 3 or later
  (licenses/COPYING.txt, notices in licenses/NOTICE.txt). The libraries in lib/ keep their own
  licenses, in licenses/.
"""

UDEV = (ROOT / 'port/linux/51-melee-unlocked-gc-adapter.rules')


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def ldd(path):
    out = subprocess.run(['ldd', str(path)], capture_output=True, text=True).stdout
    return {k: v for k, v in ldd_text(out).items() if v != 'not found'}


def ldd_text(out):
    libs = {}
    for line in out.splitlines():   # paths may hold spaces: "name => /a b/name (0x...)"
        line = line.strip()
        if ' => not found' in line:
            libs[line.split(' => ')[0]] = 'not found'
        elif ' => /' in line:
            name, rest = line.split(' => ', 1)
            libs[name] = rest.rsplit(' (0x', 1)[0]
        elif line.startswith('/'):
            path = line.rsplit(' (0x', 1)[0]
            libs[os.path.basename(path)] = path
    return libs


def bundled(name):
    return not any(name.startswith(prefix) for prefix in SYSTEM_LIBRARIES)


def shader_pak():
    shaders = ROOT / 'port/third_party/slang-shaders'
    if not (shaders / 'crt').is_dir():
        raise SystemExit(f'missing shader presets: {shaders}')
    entries = sorted((p.relative_to(shaders).as_posix(), p.read_bytes()) for p in shaders.rglob('*') if p.is_file())
    digest = hashlib.sha256()
    for entry, data in entries:
        digest.update(entry.encode('utf-8') + b'\0' + data)
    pak = bytearray(b'MUSHPAK1' + digest.hexdigest()[:16].encode('ascii') + struct.pack('<I', len(entries)))
    for entry, data in entries:
        raw = entry.encode('utf-8')
        pak += struct.pack('<H', len(raw)) + raw + struct.pack('<I', len(data)) + data
    return bytes(pak)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-linux/port/linux/full')
    parser.add_argument('--game-build-dir', type=Path, default=ROOT / 'build-linux-game')
    parser.add_argument('--out', type=Path, default=ROOT / 'release')
    parser.add_argument('--ghost-ai-dir', type=Path, default=None,
                        help='ghost_ai_<pro>.bin for each released pro plus ghost_ai_models.json (Fight a pro)')
    args = parser.parse_args()
    if sys.platform != 'linux':
        parser.error('run this inside Linux (WSL is fine)')
    version = (ROOT / 'VERSION').read_text().strip()
    exe = args.build_dir / EXE
    launcher = args.build_dir / 'launcher' / 'MeleeUnlocked'
    game = args.game_build_dir / 'melee_game.so'
    snapexcl = args.game_build_dir / 'melee_game.snapexcl'
    for need in (exe, launcher, game, snapexcl):
        if not need.is_file():
            raise SystemExit(f'missing {need} (run tools/build_linux.py first)')
    reported = subprocess.run([str(exe), '--version'], capture_output=True, text=True, timeout=60).stdout.strip()
    if reported != version:
        raise SystemExit(f'the Source Port reports {reported!r}, not {version!r}')
    if version.encode() not in launcher.read_bytes():
        raise SystemExit(f'the launcher was built before VERSION became {version}')

    name = f'MeleeUnlocked-{version}'
    stage = args.out / 'linux-stage'
    folder = stage / name
    if stage.exists():
        shutil.rmtree(stage)
    folder.mkdir(parents=True)

    # The programs, without their debug information; the game library's goes to melee_game.dbg, linked
    # back to it, as the Windows build's does (addr2line -e melee_game.dbg reads crash offsets).
    shutil.copy2(exe, folder / EXE)
    shutil.copy2(launcher, folder / 'MeleeUnlocked')
    run('strip', '--strip-unneeded', folder / EXE)
    run('strip', '--strip-unneeded', folder / 'MeleeUnlocked')
    # The game build already split them (sourceport/game/CMakeLists.txt). The debug file's sections are
    # compressed (a third of the size; gdb and addr2line read them as is), then the library's debug link
    # is written again so it carries the compressed file's CRC. Only that non-loaded section changes.
    dbg = args.game_build_dir / 'melee_game.dbg'
    if not dbg.is_file():
        raise SystemExit(f'missing {dbg} (built with melee_game)')
    shutil.copy2(game, folder / 'melee_game.so')
    shutil.copy2(dbg, folder / 'melee_game.dbg')
    shutil.copy2(snapexcl, folder / 'melee_game.snapexcl')
    run('objcopy', '--compress-debug-sections=zlib', folder / 'melee_game.dbg')
    subprocess.run(['objcopy', '--remove-section=.gnu_debuglink', '--add-gnu-debuglink=melee_game.dbg',
                    'melee_game.so'], cwd=folder, check=True)

    # Fight a pro: one network per released pro beside the Source Port, as in the Windows zip.
    for pro_file in [] if args.ghost_ai_dir is None else [f'ghost_ai_{pro}.bin' for pro in ('mang0', 'zain', 'cody', 'amsa')] + ['ghost_ai_models.json']:
        source = args.ghost_ai_dir / pro_file
        if not source.is_file():
            raise SystemExit(f'missing {source} (the pro AI files for Fight a pro)')
        shutil.copy2(source, folder / pro_file)

    # lib/: the bundled libraries and their license files.
    lib = folder / 'lib'
    lib.mkdir()
    # bin/: the shader tools the OpenGL renderer runs (renderer_gl.cpp looks in bin/ first). Without
    # them the game stopped on its first frame ("Cannot run spirv-cross") on systems lacking them.
    tools_dir = folder / 'bin'
    tools_dir.mkdir()
    shader_tools = []
    for tool in ('glslangValidator', 'spirv-cross'):
        found = shutil.which(tool)
        if not found:
            raise SystemExit(f'missing {tool} on the build machine (apt install glslang-tools spirv-cross)')
        shutil.copy2(os.path.realpath(found), tools_dir / tool)
        os.chmod(tools_dir / tool, 0o755)
        shader_tools.append(tools_dir / tool)
    libraries = {}
    for binary in (exe, launcher, game, *shader_tools):
        libraries.update(ldd(binary))
    licenses = folder / 'licenses'
    licenses.mkdir()
    packages = set()
    for soname, path in sorted(libraries.items()):
        if not bundled(soname):
            continue
        shutil.copy2(os.path.realpath(path), lib / soname)
        os.chmod(lib / soname, 0o755)
        owner = subprocess.run(['dpkg', '-S', os.path.realpath(path)], capture_output=True, text=True).stdout
        if not owner:
            owner = subprocess.run(['dpkg', '-S', path], capture_output=True, text=True).stdout
        if owner:
            packages.add(owner.split(':')[0].strip())
    for package in sorted(packages):
        copyright_file = Path('/usr/share/doc') / package / 'copyright'
        if copyright_file.is_file():
            shutil.copy2(copyright_file, licenses / f'lib-{package}.txt')
    # librashader (shader presets), loaded at run time with dlopen, so ldd does not list it. Built from
    # its source tag (port/third_party/librashader/README.txt); without it the presets are off.
    rashader = ROOT / 'port/third_party/librashader/bin/librashader.so'
    if rashader.is_file():
        shutil.copy2(rashader, lib / 'librashader.so')
        os.chmod(lib / 'librashader.so', 0o755)
    else:
        print('lib: librashader.so not built; shader presets will be off on Linux')
    print(f'lib: {len(list(lib.iterdir()))} libraries from {len(packages)} packages')
    # A program's RUNPATH only serves its own direct dependencies, so each bundled library gets its own
    # ($ORIGIN = lib/): otherwise libcurl looked for librtmp and libssl in the system's folders, which
    # failed on SteamOS (librtmp missing, a newer libssl than the bundled libcrypto).
    for path in lib.iterdir():
        run('patchelf', '--set-rpath', '$ORIGIN', path)
    run('patchelf', '--set-rpath', '$ORIGIN/lib', folder / 'melee_game.so')
    for tool in shader_tools:
        run('patchelf', '--set-rpath', '$ORIGIN/../lib', tool)
    # Prove it: with no help from the environment, every dependency the package bundles resolves to lib/.
    env = {k: v for k, v in os.environ.items() if k != 'LD_LIBRARY_PATH'}
    wrong = []
    for binary in [folder / EXE, folder / 'MeleeUnlocked', folder / 'melee_game.so', *shader_tools, *lib.iterdir()]:
        out = subprocess.run(['ldd', str(binary)], capture_output=True, text=True, env=env).stdout
        dynamic = subprocess.run(['readelf', '-d', str(binary)], capture_output=True, text=True).stdout
        needed = {line.split('[', 1)[1].split(']', 1)[0] for line in dynamic.splitlines() if '(NEEDED)' in line}
        # Each binary's own dependencies (what its RUNPATH serves); libraries only a system library
        # pulls in (the sound server's codecs) come with that system library.
        for soname, where in ldd_text(out).items():
            if soname not in needed:
                continue
            if (lib / soname).exists() and Path(where).resolve() != (lib / soname).resolve():
                wrong.append(f'{binary.name}: {soname} -> {where}')
            if where == 'not found':
                wrong.append(f'{binary.name}: {soname} not found')
    if wrong:
        raise SystemExit('bundled libraries do not resolve inside lib/:\n  ' + '\n  '.join(wrong[:20]))
    print('lib: every bundled dependency resolves inside lib/')

    # The game files, as in the Windows release.
    sys_src = ROOT / 'port/slippi_sys'
    sys_dst = folder / 'Sys'
    (sys_dst / 'GameSettings').mkdir(parents=True)
    shutil.copy2(sys_src / 'GameSettings/GALE01r2.ini', sys_dst / 'GameSettings/GALE01r2.ini')
    shutil.copy2(sys_src / 'codehandler.bin', sys_dst / 'codehandler.bin')
    shutil.copy2(sys_src / 'bootloader.gct', sys_dst / 'bootloader.gct')
    shutil.copytree(sys_src / 'GameFiles', sys_dst / 'GameFiles')
    coef = sys_src / 'GC/dsp_coef.bin'
    if hashlib.sha256(coef.read_bytes()).hexdigest() != 'c41e7d9f763da40983e83a5291cbecec1f536fd8b08ac6dda05eab33f57501ea':
        raise SystemExit(f'missing or unrecognized free DSP coefficient table: {coef}')
    (sys_dst / 'GC').mkdir()
    shutil.copy2(coef, sys_dst / 'GC/dsp_coef.bin')
    playback = ROOT / 'port/slippi_sys_playback'
    if (playback / 'codehandler.bin').is_file():
        shutil.copytree(playback, folder / 'SysPlayback', ignore=shutil.ignore_patterns('README.md', '.git*'))
        (folder / 'SysPlayback/GC').mkdir(exist_ok=True)
        shutil.copy2(coef, folder / 'SysPlayback/GC/dsp_coef.bin')
    (folder / 'Shaders').mkdir()
    (folder / 'Shaders/crt-shaders.pak').write_bytes(shader_pak())
    if (ROOT / 'lang').is_dir():
        shutil.copytree(ROOT / 'lang', folder / 'lang')
    # The launcher's picture of Bailey (the Windows launcher has her compiled in).
    (folder / 'ui').mkdir()
    shutil.copy2(ROOT / 'port/app/bailey_cut.bmp', folder / 'ui/bailey.bmp')
    ui_src = ROOT / 'port/runtime/gx/ui_sources'
    for source in ('gd_melee', 'mockups', 'menu_previews', 'vendor'):
        shutil.copytree(ui_src / source / 'assets', folder / 'ui_sources' / source)
    for sub in ('User/Slippi', 'Replays', 'MusicPacks', 'Borders', 'VideoBackgrounds', 'Mods/Discs', 'Mods/Saves'):
        (folder / sub).mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'docs/training-mods.md', folder / 'TrainingMods.md')
    shutil.copy2(UDEV, folder / UDEV.name)
    (folder / 'README.txt').write_text(README.format(version=version), encoding='utf-8')
    for src, dst in ((ROOT / 'LICENSE', 'COPYING.txt'), (ROOT / 'NOTICE', 'NOTICE.txt'),
                     (ROOT / 'port/third_party/dht/LICENCE', 'dht.txt'),
                     (ROOT / 'port/third_party/monocypher/LICENCE.md', 'monocypher.md'),
                     (ROOT / 'port/third_party/enet/LICENSE', 'enet.txt'),
                     (ROOT / 'port/third_party/imgui/LICENSE.txt', 'imgui.txt'),
                     (ROOT / 'port/third_party/stb/LICENSE', 'stb.txt'),
                     (ROOT / 'port/third_party/earcut/LICENSE', 'earcut.txt'),
                     (ROOT / 'port/third_party/librashader/LICENSE.md', 'librashader-MPL-2.0.md'),
                     (ROOT / 'docs/licenses/dsp-coefficients.txt', 'dsp-coefficients.txt'),
                     (ROOT / 'docs/licenses/hps_decode-MIT.txt', 'hps_decode.txt'),
                     (ROOT / 'docs/licenses/slippilab-MIT.txt', 'slippilab.txt'),
                     (ROOT / 'port/runtime/gx/ui_sources/gd_melee/assets/kit/SourceSans3-OFL.md', 'source-sans-3-OFL.md'),
                     (ROOT / 'docs/third-party-ui-attribution.md', 'ui-attribution.md'),
                     (ROOT / 'docs/third-party-training-mode-ce.txt', 'training-mode-ce.txt')):
        if not src.is_file():
            raise SystemExit(f'missing license text: {src}')
        shutil.copy2(src, licenses / dst)
    # Mod import tools: the system's 7-Zip and xdelta3, as the Windows release carries its own.
    tools = folder / 'tools'
    tools.mkdir()
    for wanted, names in (('7zr', ('7zz', '7zr', '7za', '7z')), ('xdelta3', ('xdelta3',))):
        found = next((shutil.which(n) for n in names if shutil.which(n)), None)
        if found:
            shutil.copy2(found, tools / wanted)
            for soname, path in ldd(found).items():
                if bundled(soname) and not (lib / soname).exists():
                    shutil.copy2(os.path.realpath(path), lib / soname)
        else:
            print(f'warning: {wanted} not found on this system; mod archives will need it installed')
    # A desktop entry players can copy to ~/.local/share/applications.
    (folder / 'MeleeUnlocked.desktop').write_text(
        '[Desktop Entry]\nType=Application\nName=Melee Unlocked\nComment=Super Smash Bros. Melee, natively\n'
        'Exec=sh -c \'cd "$(dirname "$(readlink -f "%k")")" && ./MeleeUnlocked\'\nTerminal=false\nCategories=Game;\n',
        encoding='utf-8')
    for path in folder.rglob('*'):
        if path.name.endswith('.md') and path.parent == folder and path.name != 'TrainingMods.md':
            raise SystemExit(f'private notes in the package: {path}')

    archive = args.out / f'{name}-linux-x86_64.tar.gz'
    if archive.exists():
        archive.unlink()
    with tarfile.open(archive, 'w:gz', compresslevel=9) as tar:
        tar.add(folder, arcname=name)
    total = sum(p.stat().st_size for p in folder.rglob('*') if p.is_file())
    print(f'{archive} ({archive.stat().st_size / 1e6:.1f} MB packed, {total / 1e6:.1f} MB unpacked)')


if __name__ == '__main__':
    main()
