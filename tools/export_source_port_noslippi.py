"""Export the source of the Source Port built without the Slippi layer into a clean folder.

    python tools/export_source_port_noslippi.py <out dir>

Copies exactly what that build needs (host runtime and application, game shims, CMake files, the
toolchain file, the third-party code it compiles, the build tools it runs, lang/, licences), writes
the game sources as a patch against the public decompilation plus a .gitmodules entry, and writes
MANIFEST.txt, BUILD.md and a root CMakeLists.txt for the exported tree.

Afterwards the output is scanned. The run fails, listing each hit, on: Discord webhook URLs, tokens
or keys, C:\\Users paths, email addresses, connect codes such as ABCD#123, and the word "slippi" in a
file name. Remaining text mentions of Slippi are listed for review and do not fail the run.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP_BASE = '039c4bf4ca33338c35d21901ad19b7ede19d19ad'
DECOMP_URL = 'https://github.com/doldecomp/melee.git'

# Never exported, whatever folder they are in.
BINARY_SUFFIXES = {'.exe', '.dll', '.lib', '.pdb', '.dbg', '.iso', '.gci', '.slp', '.zip', '.obj', '.o', '.a',
                   '.pyc', '.dmp', '.map', '.snapexcl', '.7z', '.gcm', '.ilk', '.exp', '.dxil', '.bin', '.log'}
# The two stand-in files of this build are the only names allowed to carry the word.
NAME_EXEMPT = {'mu_noslippi.c', 'source_noslippi.cpp'}
# The Slippi layer: host sources and headers, and the game shims.
HOST_EXCLUDED = {
    'exi_slippi.cpp', 'exi_slippi.h', 'slippi_net.cpp', 'slippi_net.h', 'slippi_online.cpp', 'slippi_online.h',
    'slippi_report.cpp', 'slippi_report.h', 'slippi_playback.cpp', 'slippi_playback.h',
    'slippi_playback_legacy.h', 'slippi_menu_codec.h', 'native_slippi_bridge.h', 'native_practice.cpp',
    'native_practice.h', 'native_practice_model.cpp', 'native_practice_model.h', 'native_replay_stream.cpp',
    'native_replay_stream.h', 'jukebox.cpp', 'jukebox.h', 'hackpack_ai.cpp',
}
GAME_EXCLUDED = {
    'mu_slippi_menu.c', 'mu_slippi_css.c', 'mu_slippi_chat.c', 'mu_slippi_splash.c', 'mu_slippi_sss.c',
    'mu_replay.c', 'mu_replay_abi.c', 'mu_record_start.inc', 'mu_practice.c', 'mu_online_rules.c',
    'record_start.c',   # the test of the replay recording
}
THIRD_PARTY = ('asio', 'dht', 'earcut', 'enet', 'imgui', 'libusb', 'monocypher', 'nlohmann', 'stb')
APP_ROOTS = ('main.cpp', 'settings_window.cpp', 'source_host.cpp', 'source_host.h', 'source_guest_boundary.cpp',
             'source_noslippi.cpp', 'source_p2p.cpp', 'source_p2p.h', 'melee_unlocked.rc', 'utf8.manifest',
             # the launcher (lobby, peer-to-peer match setup) and what it includes
             'launcher.cpp', 'launcher_mod_catalog.cpp', 'launcher_replay_data.cpp', 'launcher_lobby.cpp',
             'launcher_lobby_p2p.cpp', 'launcher_process.cpp', 'launcher_theme.cpp', 'launcher_lang.cpp')
BUILD_TOOLS = ('tools/generate_fobj_host.py', 'tools/native_major_fixture.py', 'tools/disc_offset_audit.py',
               'tools/snapshot_audio_ranges.py', 'tools/tmce/link_module.py', 'tools/fma_exact/extract_funcs.py')
RECOMP_FILES = ('recomp.py', 'analyze.py', 'dol.py', 'emit.py', 'gecko.py', 'gekko.py', 'symbols.py',
                'GALE01_symbols.txt', 'hle_list.txt')
ANIMATION_INPUTS = ('fobj.c', 'fobj.h', 'spline.c')
# Other people's code and texts: an author's address in a licence header is expected there, so the
# email and connect-code checks list hits in these for review instead of failing the run.
THIRD_PARTY_PREFIXES = ('port/third_party/', 'sourceport/game/tmce/', 'sourceport/patches/', 'melee/',
                        'LICENSE', 'NOTICE')
PRIVATE_WORDS = {'handoff', 'plan', 'note', 'notes'}
PRIVATE_NAMES = ('lobby-peer-identity', 'identity', 'status')


def private_name(name):
    lower = name.lower()
    if any(word in name for word in ('HANDOFF', 'PLAN', 'NOTE')) and name not in ('NOTICE',):
        return True
    # Lower-case forms only for documents: a source file may well be named after a plan it implements.
    document = Path(name).suffix.lower() in ('', '.md', '.txt', '.html', '.htm', '.json', '.docx', '.pdf')
    if document and set(re.split(r'[^a-z0-9]+', lower)) & PRIVATE_WORDS:
        return True
    return lower.endswith('.json') and any(word in lower for word in PRIVATE_NAMES)


def wanted(path):
    """A file may be exported at all (suffix and name rules; the folder rules are the callers')."""
    name = path.name
    if path.suffix.lower() in BINARY_SUFFIXES or name.startswith('.'):
        return False
    if '__pycache__' in path.parts or '.claude' in path.parts:
        return False
    return not private_name(name)


class Export:
    def __init__(self, out):
        self.out = out
        self.copied = []

    def copy(self, source, relative):
        target = self.out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        self.copied.append(relative.replace('\\', '/'))

    def copy_file(self, relative, required=True):
        source = ROOT / relative
        if not source.is_file():
            if required:
                raise SystemExit('missing ' + relative)
            return
        self.copy(source, relative)

    def copy_tree(self, relative, excluded=()):
        base = ROOT / relative
        if not base.is_dir():
            raise SystemExit('missing folder ' + relative)
        for source in sorted(base.rglob('*')):
            if not source.is_file() or not wanted(source) or source.name in excluded:
                continue
            rel = source.relative_to(ROOT).as_posix()
            if 'slippi' in source.name.lower() and source.name not in NAME_EXEMPT:
                continue
            self.copy(source, rel)

    def write(self, relative, text):
        target = self.out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding='utf-8', newline='\n')
        self.copied.append(relative)


def app_closure():
    """The application files the Source Port compiles, and what they include from port/app."""
    app = ROOT / 'port/app'
    pending, seen = list(APP_ROOTS), set()
    reference = re.compile(r'#\s*include\s+"([^"]+)"|\b(?:ICON|BITMAP|RT_MANIFEST)\s+"([^"]+)"')
    while pending:
        name = pending.pop()
        if name in seen or not (app / name).is_file():
            continue
        seen.add(name)
        if (app / name).suffix.lower() in ('.ico', '.bmp', '.png'):
            continue
        text = (app / name).read_text(encoding='utf-8-sig', errors='replace')
        for match in reference.finditer(text):
            target = (match.group(1) or match.group(2)).replace('\\', '/')
            if '/' not in target:
                pending.append(target)
    return sorted(seen)


def native_patch():
    """The game sources as a patch against the public decompilation: tracked changes plus the
    untracked .c and .h files, taken through a temporary index so the checkout's own is untouched."""
    decomp = ROOT / 'sourceport/extern/melee'
    scratch = Path(tempfile.mkdtemp(prefix='mu-export-'))
    env = dict(os.environ, GIT_INDEX_FILE=str(scratch / 'native.index'))

    def git(*args, with_index=True):
        return subprocess.run(['git', '-c', 'core.safecrlf=false', '-c', 'core.autocrlf=false', '-C', str(decomp), *args],
                              env=env if with_index else None, capture_output=True, check=True).stdout

    try:
        extra = git('ls-files', '--others', '--exclude-standard', '-z', with_index=False).decode().split('\0')
        extra = [name for name in extra if name and Path(name).suffix in ('.c', '.h')]
        git('read-tree', 'HEAD')
        git('add', '-u')
        if extra:
            git('add', '--', *extra)
        # The camera object is shared with the native camera code: static on the console, exported here.
        camera = 'src/melee/cm/camera.c'
        original = '/* 452C68 */ static Camera game_camera;'
        text = (decomp / camera).read_text()
        if original in text:
            text = text.replace(original, '/* 452C68 */\n#ifdef MU_NATIVE\nCamera game_camera;\n#else\n'
                                          'static Camera game_camera;\n#endif')
            temporary = scratch / 'camera.c'
            temporary.write_text(text, newline='\n')
            blob = git('hash-object', '-w', str(temporary)).decode().strip()
            git('update-index', '--cacheinfo', '100644,' + blob + ',' + camera)
        return git('diff', '--cached', '--binary', '--no-ext-diff', DECOMP_BASE)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


ROOT_CMAKE = '''cmake_minimum_required(VERSION 3.15)
project(MeleeSourcePort LANGUAGES CXX)
enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The host requires Windows x64 and MSVC")
endif()

# The instruction set baseline for every target, matching the game library: AVX2, AVX or SSE2.
set(MELEE_CPU_BASELINE "AVX2" CACHE STRING "Instruction set baseline: AVX2, AVX or SSE2")
set_property(CACHE MELEE_CPU_BASELINE PROPERTY STRINGS AVX2 AVX SSE2)
if(MELEE_CPU_BASELINE STREQUAL "SSE2")
  set(MELEE_ARCH_FLAG "")
else()
  set(MELEE_ARCH_FLAG "/arch:${MELEE_CPU_BASELINE}")
endif()

# The host's animation adapter, generated from three files of the public decompilation.
set(HOST_FOBJ "${CMAKE_CURRENT_BINARY_DIR}/generated/FObjHost.cpp")
add_custom_command(OUTPUT "${HOST_FOBJ}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_fobj_host.py" "${HOST_FOBJ}"
  DEPENDS tools/generate_fobj_host.py melee/src/sysdolphin/baselib/fobj.c
          melee/src/sysdolphin/baselib/fobj.h melee/src/sysdolphin/baselib/spline.c)
add_library(native_animation STATIC "${HOST_FOBJ}")
target_include_directories(native_animation PUBLIC native)
target_compile_features(native_animation PUBLIC cxx_std_14)
target_compile_options(native_animation PRIVATE ${MELEE_ARCH_FLAG})

# This tree holds only the Source Port without the Slippi layer.
set(MELEE_NO_SLIPPI ON CACHE BOOL "Build only melee_source, without the Slippi layer" FORCE)
add_subdirectory(port)
'''

BUILD_MD = '''# Building the Source Port

Windows x64 only. Two parts, built with two compilers, that end up in one folder:
`melee_source.exe` (the application) and `melee_game.dll` (the game, from the decompiled sources).

## Toolchains

- Visual Studio 2022 Build Tools (MSVC, x64) with a Windows 10 or 11 SDK (its `dxc.exe` is used).
- CMake 3.25 or newer, Python 3, Git.
- MinGW-w64 GCC 14 or newer with Ninja (a WinLibs build has both). The game needs GCC.

## 1. Game sources

The game is the public decompilation at a pinned commit plus this project's patch.

    git submodule update --init sourceport/extern/melee
    python tools/prepare_native_sources.py

(Outside a Git checkout: clone the decompilation into `sourceport/extern/melee`, check out the
commit named in `tools/prepare_native_sources.py`, then run that script.)

## 2. Two generated headers

The host reads the game's symbol addresses and a list of prototypes from `port/generated`. They are
generated from your own copy of the game: extract `main.dol` from your NTSC 1.02 ISO, then

    python port/recomp/recomp.py --no-slippi --dol <path to main.dol> --out port/generated

Only `hle_decls.h` and `guest_symbols.h` from that folder are used; nothing else in it is compiled.

## 3. The game library (GCC)

    cmake -S sourceport/game -B build-game -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE=%CD%/sourceport/cmake/mingw-w64-x86_64.cmake ^
      -DMELEE_MINGW_ROOT=<folder holding bin/gcc.exe> -DMELEE_PYTHON=<python.exe> -DMU_NO_SLIPPI=ON
    cmake --build build-game --target melee_game

This writes `melee_game.dll`, `melee_game.dbg` and `melee_game.snapexcl` into `build-game`.

## 4. The application (MSVC)

    cmake -S . -B build-host -G "Visual Studio 17 2022" -A x64
    cmake --build build-host --config Release --target melee_source

This writes `build-host/port/Release/melee_source.exe`. Copy the three `melee_game.*` files from
step 3 beside it (always all three together), and the `lang` folder.

Optional: NVIDIA Streamline, Intel XeSS and the NGX SDK are not part of this tree. With Streamline
unpacked under `port/third_party/streamline` and XeSS under `port/third_party/xess` the build picks
them up; `-DMELEE_ENABLE_DLSS5=ON` also needs the NGX SDK under `port/third_party/ngx`.

## 5. Running

Your own NTSC 1.02 ISO is never part of this tree. Pass it on the command line:

    melee_source.exe --iso <path to your Melee NTSC 1.02 ISO>

It can sit anywhere; nothing is written to it.
'''

SCANS = (
    ('Discord webhook', re.compile(r'discord(?:app)?\.com/api/webhooks/', re.I), False),
    ('token or key', re.compile(
        r'-----BEGIN [A-Z ]*PRIVATE KEY-----|\bgh[pousr]_[A-Za-z0-9]{30,}|\bsk-[A-Za-z0-9_\-]{20,}|\bAKIA[0-9A-Z]{16}\b|'
        r'\b[MNO][A-Za-z0-9_\-]{23,25}\.[A-Za-z0-9_\-]{6}\.[A-Za-z0-9_\-]{27,}|'
        r'(?i:\b(?:api[_-]?key|secret|token|passw(?:or)?d|authorization)\b\s*[:=]\s*["\'][A-Za-z0-9_\-\./+=]{20,}["\'])'), False),
    ('user folder path', re.compile(r'[A-Za-z]:[\\/]+Users[\\/]', re.I), False),
    ('email address', re.compile(r'\b[A-Za-z0-9._%+\-]+@[A-Za-z0-9\-]+(?:\.[A-Za-z0-9\-]+)*\.[A-Za-z]{2,}\b'), True),
    ('connect code', re.compile(r'(?<![A-Za-z0-9_#&])[A-Z]{2,8}#[0-9]{1,4}(?![0-9A-Za-z])'), True),
)
# The placeholder code the test harness of the other build passes; it is nobody's.
CONNECT_CODE_ALLOWED = {'PEER#001'}


def scan(out, allowed_codes=()):
    allowed_codes = CONNECT_CODE_ALLOWED | set(allowed_codes)
    failures, review, mentions = [], [], []
    for path in sorted(out.rglob('*')):
        if not path.is_file():
            continue
        rel = path.relative_to(out).as_posix()
        if 'slippi' in path.name.lower() and path.name not in NAME_EXEMPT:
            failures.append('file name: ' + rel)
        if path.suffix.lower() in BINARY_SUFFIXES:
            failures.append('binary file: ' + rel)
        data = path.read_bytes()
        if b'\0' in data[:4096]:
            continue   # an icon or bitmap the resource script names
        text = data.decode('utf-8', errors='replace')
        third_party = rel.startswith(THIRD_PARTY_PREFIXES)
        for label, pattern, lenient in SCANS:
            for match in pattern.finditer(text):
                hit = match.group(0)
                if label == 'connect code' and hit in allowed_codes:
                    continue
                line = text.count('\n', 0, match.start()) + 1
                entry = '%s: %s:%d: %s' % (label, rel, line, hit[:80])
                (review if lenient and third_party else failures).append(entry)
        count = len(re.findall(r'slippi', text, re.I))
        if count:
            mentions.append((rel, count))
    return failures, review, mentions


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('out', type=Path, help='folder to create (must be empty or absent)')
    parser.add_argument('--publish-tree', type=Path, default=ROOT.parent / 'melee-unlocked-publish-v085',
                        help='the publish tree that holds tools/prepare_native_sources.py')
    parser.add_argument('--allow-connect-code', action='append', default=[], metavar='CODE',
                        help='a made-up example code (such as the one a help text shows) that is not a finding; repeatable')
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        raise SystemExit(str(out) + ' is not empty')
    if ROOT in out.parents or out == ROOT:
        raise SystemExit('export outside the source tree')
    out.mkdir(parents=True, exist_ok=True)
    export = Export(out)

    for name in ('LICENSE', 'NOTICE', 'VERSION'):
        export.copy_file(name)
    export.write('CMakeLists.txt', ROOT_CMAKE)
    export.copy_tree('lang')
    for header in sorted((ROOT / 'native').glob('*.h')):
        export.copy(header, 'native/' + header.name)
    for name in ANIMATION_INPUTS:   # the three files tools/generate_fobj_host.py reads
        export.copy_file('melee/src/sysdolphin/baselib/' + name)

    export.copy_file('port/CMakeLists.txt')
    for name in app_closure():
        export.copy_file('port/app/' + name)
    # The launcher's resource script and what it embeds (icons, bitmaps), which no #include reaches,
    # and the trace viewer the launcher links.
    for pattern in ('*.rc', '*.ico', '*.bmp', 'launcher_trace_view.*'):
        for path in sorted((ROOT / 'port/app').glob(pattern)):
            export.copy_file('port/app/' + path.name)
    if (ROOT / 'port/app/stock_icons').is_dir():
        export.copy_tree('port/app/stock_icons')
    export.copy_tree('port/runtime', HOST_EXCLUDED)
    export.copy_file('port/dlss5_forwarder/forwarder.cpp', required=False)
    for name in RECOMP_FILES:
        export.copy_file('port/recomp/' + name)
    for name in THIRD_PARTY:
        export.copy_tree('port/third_party/' + name)

    export.copy_tree('sourceport/game', GAME_EXCLUDED)
    export.copy_tree('sourceport/cmake')
    if (ROOT / 'sourceport/tools').is_dir():
        export.copy_tree('sourceport/tools')
    for name in BUILD_TOOLS:
        export.copy_file(name)
    # The peer-to-peer modes: protocol document, the two-instance test and its fault proxy, the bot script, tests.
    for name in ('docs/mu-net-protocol.md', 'tools/p2p_pair.py', 'tools/p2p_launcher_pair.py', 'tools/gecko_targets.py', 'tools/net_fault_proxy.py', 'tools/melee_iso.py',
                 'port/scripts/p2p_bot.txt', 'port/tests/mu_net_test.cpp', 'port/tests/launcher_lobby_p2p_test.cpp',
                 # the shared regression checks the standalone build also runs (port/CMakeLists.txt)
                 'port/tests/offline_input_delay_test.cpp', 'port/tests/stage_dat_safety_test.cpp',
                 'port/tests/user_gecko_test.cpp', 'port/tests/disc_archive_test.cpp',
                 'port/tests/disc_fst_paths_test.cpp', 'port/tests/cosmetic_mods_test.cpp'):
        export.copy_file(name, required=False)
    prepare = args.publish_tree / 'tools/prepare_native_sources.py'
    if not prepare.is_file():
        raise SystemExit('missing ' + str(prepare))
    export.copy(prepare, 'tools/prepare_native_sources.py')

    patch = native_patch()
    (out / 'sourceport/patches').mkdir(parents=True, exist_ok=True)
    (out / 'sourceport/patches/melee-native.patch').write_bytes(patch)
    export.copied.append('sourceport/patches/melee-native.patch')
    export.write('.gitmodules', '[submodule "sourceport/extern/melee"]\n\tpath = sourceport/extern/melee\n'
                                '\turl = %s\n' % DECOMP_URL)
    export.write('BUILD.md', BUILD_MD)

    lines = ['Source Port without the Slippi layer: exported files', 'decompilation base ' + DECOMP_BASE, '']
    for rel in sorted(set(export.copied)):
        data = (out / rel).read_bytes()
        lines.append('%s  %8d  %s' % (hashlib.sha256(data).hexdigest(), len(data), rel))
    (out / 'MANIFEST.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')

    failures, review, mentions = scan(out, args.allow_connect_code)
    print('exported %d files to %s (native patch %d bytes)' % (len(set(export.copied)) + 1, out, len(patch)))
    if mentions:
        print('\ntext mentions of Slippi to review (%d files, %d mentions):' % (len(mentions), sum(c for _, c in mentions)))
        for rel, count in mentions:
            print('  %4d  %s' % (count, rel))
    if review:
        print('\nin third-party files, for review (%d):' % len(review))
        for entry in review:
            print('  ' + entry)
    if failures:
        print('\nFAILED, %d findings:' % len(failures))
        for entry in failures:
            print('  ' + entry)
        return 1
    print('\nscan clean')
    return 0


if __name__ == '__main__':
    sys.exit(main())
