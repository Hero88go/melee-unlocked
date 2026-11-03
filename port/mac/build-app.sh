#!/bin/bash
# M2 exception: direct Apple clang, serial compilation; never builds dependencies/game.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
exec python3 - "$root" <<'PY'
from pathlib import Path
import hashlib, os, plistlib, shlex, shutil, subprocess, sys, time
root = Path(sys.argv[1])
build = root / 'build-mac'
build.mkdir(exist_ok=True)
objects = build / 'graphics-objects'
objects.mkdir(exist_ok=True)
sdl = root.parent / 'deps/sdl2'
release = root / 'spike/MeleeUnlocked-0.9.52'
required = [sdl/'include/SDL2/SDL.h', sdl/'lib/libSDL2-2.0.0.dylib',
            root/'spike/melee_game.so', release/'melee_game.snapexcl', release/'Sys',
            root.parent/'deps/tools/bin/glslangValidator', root.parent/'deps/tools/bin/spirv-cross']
for path in required:
    if not path.exists(): sys.exit(f'Missing on-disk dependency: {path}')
def memory_gate():
    while True:
        output = subprocess.check_output(['memory_pressure'], text=True)
        line = next(line for line in output.splitlines() if 'free percentage' in line)
        print(line, flush=True)
        if int(line.split()[-1].rstrip('%')) >= 15: return
        print('Below 15%; waiting 60 seconds', flush=True)
        time.sleep(60)
version = (root/'VERSION').read_text().splitlines()[0].strip()
flags = ['-arch','x86_64','-mmacosx-version-min=15.0','-std=c++17','-O2','-g',
         '-Wall','-Wextra','-fno-strict-aliasing','-ffp-contract=on',
         '-DMELEE_NO_SLIPPI','-DMELEE_LINUX_SDL','-DMELEE_LINUX_GRAPHICS',
         '-DMELEE_LINUX_UNLOCKED',f'-DMELEE_PORT_VERSION="{version}"']
for path in ['port/linux','port/runtime/abi','port/runtime/hle','port/runtime/host',
             'port/runtime/ppc','port/runtime/gx','native',str(sdl/'include'),str(sdl/'include/SDL2')]:
    flags += ['-I'+path]
generated = build/'generated/FObjHost.cpp'
pending = build/'generated/FObjHost.pending.cpp'
subprocess.run(['python3','tools/generate_fobj_host.py',str(pending)],check=True)
if not generated.exists() or generated.read_bytes() != pending.read_bytes():
    pending.replace(generated)
else:
    pending.unlink()
sources = ['port/mac/main.cpp','port/mac/mac_gl.mm','port/mac/elf_loader.cpp','port/linux/host_services.cpp',
           'port/linux/input_script.cpp','port/runtime/hle/ax_ucode.cpp',
           'port/linux/renderer_gl.cpp','port/linux/gx_host_compat.cpp','port/linux/renderer_presenter.cpp',
           'build-mac/generated/FObjHost.cpp']
sources += [f'port/runtime/gx/{name}.cpp' for name in
            ['gx_core','gx_shader','gx_texture','native_draw_audit','native_pose_bridge','authored_pose','subframe']]
fingerprint = hashlib.sha256(repr(flags).encode()).hexdigest()
object_paths = []
for source in sources:
    obj = objects / (source.replace('/','_') + '.o')
    dep, stamp = obj.with_suffix('.d'), obj.with_suffix('.flags')
    dependencies = [root/source]
    if dep.exists():
        dependencies += [Path(p) for p in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1])]
    fresh = (obj.exists() and stamp.exists() and stamp.read_text() == fingerprint and
             all(p.exists() and p.stat().st_mtime <= obj.stat().st_mtime for p in dependencies))
    if not fresh:
        memory_gate()
        print('Compile', source, flush=True)
        subprocess.run(['/usr/bin/clang++',*flags,'-MMD','-MF',str(dep),'-c',source,'-o',str(obj)],check=True)
        stamp.write_text(fingerprint)
    object_paths.append(str(obj))
exe = build/'MeleeUnlocked'
memory_gate()
subprocess.run(['/usr/bin/clang++','-arch','x86_64','-mmacosx-version-min=15.0',*object_paths,
                '-L'+str(sdl/'lib'),'-lSDL2-2.0.0','-framework','OpenGL','-framework','AppKit',
                '-Wl,-rpath,@executable_path/../Frameworks','-Wl,-rpath,'+str(sdl/'lib'),
                '-Wl,-pagezero_size,0x10000','-o',str(exe)],check=True)
app = build/'MeleeUnlocked.app'
if app.exists(): shutil.rmtree(app)
contents = app/'Contents'
for name in ['MacOS','Frameworks','Resources/bin']: (contents/name).mkdir(parents=True)
shutil.copy2(exe,contents/'MacOS/MeleeUnlocked')
shutil.copy2(sdl/'lib/libSDL2-2.0.0.dylib',contents/'Frameworks/libSDL2-2.0.0.dylib',follow_symlinks=True)
resources = contents/'Resources'
shutil.copy2(root/'spike/melee_game.so',resources/'melee_game.so')
shutil.copy2(release/'melee_game.snapexcl',resources/'melee_game.snapexcl')
shutil.copytree(release/'Sys',resources/'Sys',symlinks=False)
for tool in ['glslangValidator','spirv-cross']:
    # Self-contained universal builds (glslang 15.0.0, SPIRV-Cross vulkan-sdk-1.3.296.0), system libraries only.
    shutil.copy2(root.parent/'deps/tools/bin'/tool, resources/'bin'/tool, follow_symlinks=True)
if (root/'spike/shader-seed').is_dir():
    shutil.copytree(root/'spike/shader-seed',resources/'shader-seed',symlinks=False)
(resources/'EXPERIMENTAL.txt').write_text('Experimental, not tested yet\n')
info = dict(CFBundleIdentifier='co.yougame.meleeunlocked',CFBundleName='Melee Unlocked',
            CFBundleDisplayName='Melee Unlocked - Experimental, not tested yet',
            CFBundleShortVersionString='0.9.52',CFBundleVersion='0.9.52',
            CFBundleExecutable='MeleeUnlocked',CFBundlePackageType='APPL',
            LSMinimumSystemVersion='15.0',LSArchitecturePriority=['x86_64'],
            NSHighResolutionCapable=True,NSPrincipalClass='NSApplication')
with (contents/'Info.plist').open('wb') as f: plistlib.dump(info,f)
assert not any(p.is_symlink() for p in app.rglob('*')), 'Bundle contains symlinks'
print(f'Unsigned experimental bundle: {app}',flush=True)
subprocess.run(['/usr/bin/otool','-L',str(contents/'MacOS/MeleeUnlocked')],check=True)
PY
