"""Incremental macOS host build: x86-64 (Rosetta) or, with --arch arm64, native Apple silicon.
Staging hooks use build-mac-full-hook, arm64 builds add -arm64, squad builds add -squad."""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, shlex, shutil, subprocess, sys, time
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build-mac-full'
def arch_env():
    # package_full.py (a subprocess) resolves the same architecture, build directory and game library.
    return os.environ.get('MELEE_MAC_ARCH', 'x86_64')

# The AArch64 game library is the same source built by the AArch64 cross GCC (port/mac/build_all.sh).
def game_library():
    default = ROOT / ('build-mac-input' + ('-arm64' if arch_env() == 'arm64' else '')) / 'melee_game.so'
    return Path(os.environ.get('MELEE_GAME_LIBRARY', str(default))).resolve()
GAME_LIBRARY = game_library()
SDL = ROOT.parent / 'deps/sdl2'
# Static x86_64 libusb (include/libusb-1.0, lib/libusb-1.0.a): when present, the GameCube adapter is read
# directly, as Slippi Dolphin does on the Mac; without it only through the YouGame app.
LIBUSB = ROOT.parent / 'deps/libusb'

def squad_env():
    return os.environ.get('MELEE_SQUAD_STRIKE', 'OFF').upper() in ('ON', '1', 'TRUE')

def build_dir(agent_test_hook=False, squad=None, arch=None):
    # Squad hosts get their own directory so a squad object never links into a vanilla app; so do
    # arm64 hosts.
    squad = squad_env() if squad is None else squad
    arch = arch_env() if arch is None else arch
    return ROOT / ('build-mac-full' + ('-hook' if agent_test_hook else '') + ('-arm64' if arch == 'arm64' else '') +
                   ('-squad' if squad else ''))

def memory_gate():
    while True:
        output = subprocess.check_output(['memory_pressure'], text=True)
        line = next(s for s in output.splitlines() if 'free percentage' in s)
        print(line, flush=True)
        if int(line.split()[-1].rstrip('%')) >= 15:
            return
        print('Below 15%; waiting 60 seconds', flush=True)
        time.sleep(60)

def generate(command, path):
    pending = path.with_suffix('.pending' + path.suffix)
    pending.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([*command, str(pending)], check=True)
    if not path.exists() or pending.read_bytes() != path.read_bytes():
        pending.replace(path)
    else:
        pending.unlink()

def main():
    global BUILD, GAME_LIBRARY
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agent-test-hook', action='store_true', help='STAGING ONLY: enable agent-test.json hooks')
    parser.add_argument('--doubles', action='store_true', help='experimental console Doubles; requires a matching Doubles game library')
    parser.add_argument('--host-only', action='store_true', help='Compile and link the host without game assets or packaging')
    parser.add_argument('--yougame-mac-tray-from', help='First shipped Mac YouGame Staging version supporting --yougame-tray (X.Y.Z)')
    parser.add_argument('--yougame-mac-prod-tray-from', help='First shipped Mac YouGame (production) version supporting --yougame-tray (X.Y.Z)')
    parser.add_argument('--yougame-start-staging', action='store_true', help='test builds: start and link YouGame Staging instead of YouGame')
    # Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    parser.add_argument('--arch', choices=['x86_64', 'arm64'], default=arch_env(),
                        help='x86_64 runs under Rosetta; arm64 is native on Apple silicon')
    parser.add_argument('--squad-strike', action='store_true',
                        default=squad_env(),
                        help='MELEE_SQUAD_STRIKE: enable squad host ABI (pair with an ON game library)')
    args = parser.parse_args()
    for flag, value in [('--yougame-mac-tray-from', args.yougame_mac_tray_from), ('--yougame-mac-prod-tray-from', args.yougame_mac_prod_tray_from)]:
        if not value:
            continue
        parts = value.split('.')
        if len(parts) != 3 or any(not p.isascii() or not p.isdigit() or len(p) > 5 or int(p) > 65535 for p in parts):
            parser.error(flag + ' must be three version components, each 0..65535')
    if args.squad_strike:
        os.environ['MELEE_SQUAD_STRIKE'] = 'ON'  # package_full (subprocess) resolves the same directory
    os.environ['MELEE_MAC_ARCH'] = args.arch  # package_full (subprocess) resolves the same architecture
    GAME_LIBRARY = game_library()
    BUILD = build_dir(args.agent_test_hook, args.squad_strike)
    os.chdir(ROOT)
    BUILD.mkdir(exist_ok=True)
    gen = BUILD / 'gen/port-generated'
    if BUILD != build_dir(False, False, 'x86_64'):
        # The recomp is identical; only host/runtime objects differ between variants (and architectures).
        gen.mkdir(parents=True, exist_ok=True)
        for name in ['hle_decls.h', 'guest_symbols.h']:
            shutil.copy2(build_dir(False, False, 'x86_64')/'gen/port-generated'/name, gen/name)
    needed_inputs = [SDL/'include/SDL2/SDL.h', SDL/'lib/libSDL2-2.0.0.dylib', gen/'hle_decls.h', gen/'guest_symbols.h']
    if not args.host_only:
        needed_inputs += [GAME_LIBRARY, GAME_LIBRARY.with_suffix('.snapexcl'), GAME_LIBRARY.parent/'Sys/GC/dsp_coef.bin']
    for needed in needed_inputs:
        if not needed.exists():
            sys.exit(f'Missing on-disk input: {needed}')
    generate(['python3', 'tools/generate_fobj_host.py'], BUILD/'gen/FObjHost.cpp')
    generate(['python3', 'tools/native_recording_codes.py'], BUILD/'gen/native_recording_codes.h')
    sources = []
    for directory in ['ppc', 'hle', 'host', 'gx']:
        sources += sorted(Path('port/runtime', directory).glob('*.cpp'))
    removed = {'leaf_stencils', 'leaf_translator', 'ram_translator', 'gx_d3d11', 'gx_d3d12', 'gx_dlss5',
               'gx_dlss5_scaling', 'gx_dxr_gpu_scene', 'gx_dxr_path_tracer', 'gx_streamline', 'gx_xess', 'gx_fsr', 'gx_fsr_vk', 'rtx_scene', 'rtx_tracer', 'rtx_melee_integrator',
               'gx_backend', 'pc_settings_dx11', 'audio'}
    sources = [p for p in sources if p.stem not in removed]
    sources += sorted(Path('port/runtime/mu_net').glob('*.cpp'))
    sources += [Path(p) for p in ['port/third_party/slippilib/SlippiGame.cpp', 'port/linux/compat/compat.cpp',
        'port/linux/compat/winhttp_ws.cpp', 'port/linux/xinput_sdl.cpp', 'port/linux/hid_linux.cpp', 'port/linux/renderer_gl.cpp',
        'port/linux/renderer_presenter.cpp', 'port/mac/mac_gl.mm', 'port/mac/elf_loader.cpp', str(BUILD.relative_to(ROOT)/'gen/FObjHost.cpp'),
        'port/app/main.cpp', 'port/app/source_host.cpp', 'port/app/source_guest_boundary.cpp', 'port/app/source_p2p.cpp']]
    sources += sorted(Path('port/linux/full').glob('*.cpp'))
    sources += [Path('port/mac/embed_mac.mm'), Path('port/mac/disc_choice_mac.mm')]
    sources += sorted(Path('port/third_party/enet').glob('*.c'))
    sources += sorted(Path('port/third_party/monocypher').glob('*.c'))
    # .slpz replays (main.cpp playable_replay and the compress path): replay_slpz.cpp and zstd both ways, as
    # port/app/disc_rvz.cmake builds them for Windows and Linux.
    sources += [Path('port/app/replay_slpz.cpp')]
    for part in ['common', 'decompress', 'compress']:
        sources += sorted(Path('port/third_party/zstd', part).glob('*.c'))
    sources += [Path('port/third_party/imgui', p) for p in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp',
        'imgui_widgets.cpp','backends/imgui_impl_sdl2.cpp','backends/imgui_impl_opengl3.cpp','backends/imgui_impl_vulkan.cpp']]
    version = (ROOT/'VERSION').read_text().strip()
    native = args.arch == 'arm64'
    flags = ['-arch',args.arch,'-mmacosx-version-min=15.0','-O2','-g','-fno-strict-aliasing',
             '-ffp-contract=off','-pthread','-Wno-unknown-pragmas','-Wno-multichar','-Wno-deprecated-declarations']
    cpp = ['-std=c++17','-include','port/linux/compat/windows.h','-DMELEE_LINUX','-DGX_PC_SETTINGS',
           '-DMELEE_SOURCE_PORT', f'-DMELEE_PORT_VERSION="{version}"',
           '-DMELEE_CPU_BASELINE_NAME="%s"' % ('AArch64' if native else 'AVX2'),
           '-DMELEE_PLAYBACK_GCT_BASE=0u','-DMELEE_PLAYBACK_GCT_SIZE=0u','-DGX_SHADER_CACHE_VERSION="mac-gl-1"',
           # The Vulkan renderer (gx_vulkan.cpp, MoltenVK on the Mac) opens the loader at run time: headers only.
           '-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES']
    if args.doubles:
        cpp += ['-DMELEE_DOUBLES=1']
    if args.yougame_mac_tray_from:
        cpp += [f'-DMELEE_YOUGAME_MAC_TRAY_FROM="{args.yougame_mac_tray_from}"']
    if args.yougame_mac_prod_tray_from:
        cpp += [f'-DMELEE_YOUGAME_MAC_PROD_TRAY_FROM="{args.yougame_mac_prod_tray_from}"']
    if args.yougame_start_staging:
        cpp += ['-DMELEE_YOUGAME_START_STAGING=1']
    if args.squad_strike:
        cpp += ['-DMU_SQUAD=1']
    gc_adapter = (LIBUSB/'lib/libusb-1.0.a').exists()
    if not gc_adapter:
        print('No libusb at', LIBUSB, '- the GameCube adapter is read only through the YouGame app', flush=True)
    for p in ['port/linux/compat','port/linux','port/app','port/runtime/ppc','port/runtime/hle','port/runtime/host',
              'port/runtime/gx','port/runtime/mu_net','port/runtime/abi','port/third_party','native',str(BUILD/'gen'),
              str(gen),'port/third_party/enet/include','port/third_party/monocypher','port/third_party/imgui','port/third_party/onnxruntime/include',
              'port/third_party/imgui/backends','port/third_party/vulkan-headers/include',str(SDL/'include'),str(SDL/'include/SDL2')]:
        cpp += ['-I'+p]
    cflags = ['-DHAS_SOCKLEN_T=1','-DHAS_POLL=1','-DHAS_FCNTL=1','-DHAS_MSGHDR_FLAGS=1',
              '-DHAS_GETADDRINFO=1','-DHAS_GETNAMEINFO=1','-DHAS_INET_PTON=1','-DHAS_INET_NTOP=1',
              '-Iport/third_party/enet/include']
    objects = BUILD/'objects'
    objects.mkdir(exist_ok=True)
    def compile_one(source):
        obj = objects/(str(source).replace('/','_')+'.o')
        dep, stamp = obj.with_suffix('.d'), obj.with_suffix('.flags')
        opts = [*flags, *(['-std=c++17'] if source.suffix == '.mm' else cflags if source.suffix == '.c' else cpp)]
        if source.name in ('embed_mac.mm', 'disc_choice_mac.mm'):
            # AppKit must not see the Win32 compatibility typedefs/macros.
            opts = [*flags, '-std=c++17', '-fblocks', '-fno-objc-arc',
                    '-I'+str(SDL/'include'), '-I'+str(SDL/'include/SDL2')]
        # Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
        if args.squad_strike and source.suffix == '.mm':
            opts += ['-DMU_SQUAD=1']  # AppKit units also see guarded host header layouts.
        if args.agent_test_hook and (str(source).startswith(('port/runtime/', 'port/app/',
                                                            'port/linux/', 'port/mac/')) or
                                    source == Path('port/third_party/slippilib/SlippiGame.cpp')):
            # Include runtime platform replacements and host app units, as in CMake.
            opts += ['-DYOUGAME_AGENT_TEST_HOOK']
        if gc_adapter and source.as_posix() == 'port/runtime/host/gc_adapter.cpp':
            opts += ['-DMELEE_MAC_GC_ADAPTER=1', '-I'+str(LIBUSB/'include/libusb-1.0')]
        if source.as_posix() != 'port/app/main.cpp' and not native:
            opts += ['-mavx2', '-mfma']
        if source.as_posix().startswith('port/third_party/zstd/') or source.as_posix() == 'port/app/replay_slpz.cpp':
            # disc_rvz.cmake's flags: no assembly (the Huffman fast path is a .S file), zstd's xxhash kept private.
            opts += ['-DZSTD_DISABLE_ASM=1', '-DXXH_NAMESPACE=ZSTD_'] + (['-w'] if source.suffix == '.c' else [])
        fingerprint = hashlib.sha256(repr(opts).encode()).hexdigest()
        dependencies = [source]
        if dep.exists():
            dependencies += [Path(p) for p in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1])]
        fresh = (obj.exists() and stamp.exists() and stamp.read_text() == fingerprint and
                 all(p.exists() and p.stat().st_mtime <= obj.stat().st_mtime for p in dependencies))
        if not fresh:
            memory_gate()
            print('Compile',source,flush=True)
            subprocess.run(['/usr/bin/clang' if source.suffix == '.c' else '/usr/bin/clang++',*opts,
                            '-MMD','-MF',str(dep),'-c',str(source),'-o',str(obj)],check=True)
            stamp.write_text(fingerprint)
        return str(obj)
    # Keep failures bounded: submit two jobs at a time; do not continue a large batch after errors.
    paths = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for i in range(0,len(sources),2):
            paths += list(pool.map(compile_one,sources[i:i+2]))
    memory_gate()
    app_objects = [p for p in paths if Path(p).name.startswith('port_app_')]
    library = BUILD/'libfull_host.a'
    if library.exists():
        library.unlink()  # ar must not retain objects removed from the source list
    subprocess.run(['/usr/bin/ar', 'rcs', str(library), *[p for p in paths if p not in app_objects]], check=True)
    subprocess.run(['/usr/bin/clang++',*flags,*app_objects,str(library),'-L'+str(SDL/'lib'),'-lSDL2-2.0.0','-lcurl','-liconv',
                    *([str(LIBUSB/'lib/libusb-1.0.a'),'-lobjc','-framework','IOKit'] if gc_adapter else []),
                    '-framework','OpenGL','-framework','AppKit','-framework','CoreGraphics','-framework','Security',
                    '-framework','CoreServices','-framework','CoreFoundation',
                    # Rosetta: free the low addresses for the console window. A native process keeps a
                    # 4 GB page zero, and the window moves above it instead (port/mac/elf_loader.cpp).
                    *([] if native else ['-Wl,-pagezero_size,0x10000']),
                    '-Wl,-rpath,@executable_path/../Frameworks','-Wl,-rpath,'+str(SDL/'lib'),
                    '-o',str(BUILD/'MeleeUnlocked')],check=True)
    (BUILD/'build-manifest.json').write_text(json.dumps({'version':version,
        'build_script':'port/mac/build_full.py',
        'host_entry':'port/app/main.cpp',
        'agent_test_hook':args.agent_test_hook,
        'squad_strike':args.squad_strike,
        'channel':'STAGING ONLY' if args.agent_test_hook else 'release',
        'host_sha256':hashlib.sha256((BUILD/'MeleeUnlocked').read_bytes()).hexdigest()},indent=2)+'\n')
    print('Full host:',BUILD/'MeleeUnlocked',flush=True)
    if args.host_only:
        return
    subprocess.run([sys.executable, '-B', str(ROOT/'port/mac/package_full.py'),
                    *(['--agent-test-hook'] if args.agent_test_hook else [])], check=True)
if __name__ == '__main__':
    main()
