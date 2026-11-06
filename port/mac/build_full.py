"""Incremental x86-64 macOS host build; staging hooks use build-mac-full-hook."""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, shlex, shutil, subprocess, sys, time
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build-mac-full'
DEFAULT_GAME_LIBRARY = Path('/Volumes/YGWorktrees/build-out/melee-ports/mu-0956-linux-game-nosp/melee_game.so')
GAME_LIBRARY = Path(os.environ.get('MELEE_GAME_LIBRARY', str(DEFAULT_GAME_LIBRARY))).resolve()
SDL = ROOT.parent / 'deps/sdl2'

def build_dir(agent_test_hook=False):
    return ROOT / ('build-mac-full-hook' if agent_test_hook else 'build-mac-full')

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
    global BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agent-test-hook', action='store_true', help='STAGING ONLY: enable agent-test.json hooks')
    parser.add_argument('--host-only', action='store_true', help='Compile and link the host without game assets or packaging')
    args = parser.parse_args()
    BUILD = build_dir(args.agent_test_hook)
    os.chdir(ROOT)
    BUILD.mkdir(exist_ok=True)
    gen = BUILD / 'gen/port-generated'
    if args.agent_test_hook:
        # The recomp is identical; only host/runtime objects differ between variants.
        gen.mkdir(parents=True, exist_ok=True)
        for name in ['hle_decls.h', 'guest_symbols.h']:
            shutil.copy2(build_dir()/'gen/port-generated'/name, gen/name)
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
               'gx_dlss5_scaling', 'gx_dxr_gpu_scene', 'gx_dxr_path_tracer', 'gx_streamline', 'gx_xess', 'rtx_scene', 'rtx_tracer', 'rtx_melee_integrator',
               'gx_backend', 'pc_settings_dx11', 'audio'}
    sources = [p for p in sources if p.stem not in removed]
    sources += sorted(Path('port/runtime/mu_net').glob('*.cpp'))
    sources += [Path(p) for p in ['port/third_party/slippilib/SlippiGame.cpp', 'port/linux/compat/compat.cpp',
        'port/linux/compat/winhttp_ws.cpp', 'port/linux/xinput_sdl.cpp', 'port/linux/hid_linux.cpp', 'port/linux/renderer_gl.cpp',
        'port/linux/renderer_presenter.cpp', 'port/mac/mac_gl.mm', 'port/mac/elf_loader.cpp', str(BUILD.relative_to(ROOT)/'gen/FObjHost.cpp'),
        'port/app/main.cpp', 'port/app/source_host.cpp', 'port/app/source_guest_boundary.cpp', 'port/app/source_p2p.cpp']]
    sources += sorted(Path('port/linux/full').glob('*.cpp'))
    sources += [Path('port/mac/embed_mac.mm')]
    sources += sorted(Path('port/third_party/enet').glob('*.c'))
    sources += sorted(Path('port/third_party/monocypher').glob('*.c'))
    sources += [Path('port/third_party/imgui', p) for p in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp',
        'imgui_widgets.cpp','backends/imgui_impl_sdl2.cpp','backends/imgui_impl_opengl3.cpp']]
    version = (ROOT/'VERSION').read_text().strip()
    flags = ['-arch','x86_64','-mmacosx-version-min=15.0','-O2','-g','-fno-strict-aliasing',
             '-ffp-contract=off','-pthread','-Wno-unknown-pragmas','-Wno-multichar','-Wno-deprecated-declarations']
    cpp = ['-std=c++17','-include','port/linux/compat/windows.h','-DMELEE_LINUX','-DGX_PC_SETTINGS',
           '-DMELEE_SOURCE_PORT', f'-DMELEE_PORT_VERSION="{version}"','-DMELEE_CPU_BASELINE_NAME="AVX2"',
           '-DMELEE_PLAYBACK_GCT_BASE=0u','-DMELEE_PLAYBACK_GCT_SIZE=0u','-DGX_SHADER_CACHE_VERSION="mac-gl-1"']
    for p in ['port/linux/compat','port/linux','port/app','port/runtime/ppc','port/runtime/hle','port/runtime/host',
              'port/runtime/gx','port/runtime/mu_net','port/runtime/abi','port/third_party','native',str(BUILD/'gen'),
              str(gen),'port/third_party/enet/include','port/third_party/monocypher','port/third_party/imgui',
              'port/third_party/imgui/backends',str(SDL/'include'),str(SDL/'include/SDL2')]:
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
        if source.name == 'embed_mac.mm':
            # AppKit must not see the Win32 compatibility typedefs/macros.
            opts = [*flags, '-std=c++17', '-fblocks', '-fno-objc-arc',
                    '-I'+str(SDL/'include'), '-I'+str(SDL/'include/SDL2')]
        if args.agent_test_hook and (str(source).startswith(('port/runtime/', 'port/app/',
                                                            'port/linux/', 'port/mac/')) or
                                    source == Path('port/third_party/slippilib/SlippiGame.cpp')):
            # Include runtime platform replacements and host app units, as in CMake.
            opts += ['-DYOUGAME_AGENT_TEST_HOOK']
        if source.as_posix() != 'port/app/main.cpp':
            opts += ['-mavx2', '-mfma']
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
                    '-framework','OpenGL','-framework','AppKit','-framework','CoreGraphics','-framework','Security','-Wl,-pagezero_size,0x10000',
                    '-Wl,-rpath,@executable_path/../Frameworks','-Wl,-rpath,'+str(SDL/'lib'),
                    '-o',str(BUILD/'MeleeUnlocked')],check=True)
    (BUILD/'build-manifest.json').write_text(json.dumps({'version':version,
        'build_script':'port/mac/build_full.py',
        'host_entry':'port/app/main.cpp',
        'agent_test_hook':args.agent_test_hook,
        'channel':'STAGING ONLY' if args.agent_test_hook else 'release',
        'host_sha256':hashlib.sha256((BUILD/'MeleeUnlocked').read_bytes()).hexdigest()},indent=2)+'\n')
    print('Full host:',BUILD/'MeleeUnlocked',flush=True)
    if args.host_only:
        return
    subprocess.run([sys.executable, '-B', str(ROOT/'port/mac/package_full.py'),
                    *(['--agent-test-hook'] if args.agent_test_hook else [])], check=True)
if __name__ == '__main__':
    main()
