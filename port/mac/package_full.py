"""Unsigned M3 app; package existing inputs only, never fetch/build game or dependencies."""
from pathlib import Path
from build_full import GAME_LIBRARY, build_dir
import argparse, hashlib, json, os, plistlib, runpy, shutil, subprocess
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-mac-full'
APP = BUILD/'MeleeUnlocked.app'
# The ELF, exclusions, and Sys assets must come from the same native build.
GAME_ASSETS = GAME_LIBRARY.parent

def copy(src, dst):
    if not src.exists():
        raise SystemExit(f'Missing on-disk packaging input: {src}')
    dst.parent.mkdir(parents=True, exist_ok=True)
    if src.is_dir():
        shutil.copytree(src, dst, symlinks=False, ignore=shutil.ignore_patterns('.git*', 'README.md'))
    else:
        shutil.copy2(src, dst, follow_symlinks=True)

def main():
    global BUILD, APP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agent-test-hook', action='store_true', help='Package the STAGING ONLY hook variant')
    parser.add_argument('--output-dir', type=Path, help='Destination containing MeleeUnlocked.app (default: build directory)')
    args = parser.parse_args()
    BUILD = build_dir(args.agent_test_hook)
    APP = (args.output_dir.resolve() if args.output_dir else BUILD)/'MeleeUnlocked.app'
    if not (BUILD/'MeleeUnlocked').is_file():
        raise SystemExit('Build the full host first: bash port/mac/build-full.sh')
    version = (ROOT/'VERSION').read_text().strip()
    manifest = BUILD/'build-manifest.json'
    if not manifest.is_file():
        raise SystemExit('Missing full-host build manifest; rebuild before packaging 0.9.56')
    built = json.loads(manifest.read_text())
    if (built.get('build_script') != 'port/mac/build_full.py' or
        built.get('host_entry') != 'port/app/main.cpp'):
        raise SystemExit('Packaging requires build_full.py with port/app/main.cpp; dev spike hosts cannot ship')
    if built.get('version') != version or built.get('host_sha256') != hashlib.sha256((BUILD/'MeleeUnlocked').read_bytes()).hexdigest():
        raise SystemExit('Full host does not match the current version/build manifest; rebuild first')
    artifact_hook = b'agent-test.json' in (BUILD/'MeleeUnlocked').read_bytes()
    if built.get('agent_test_hook') != args.agent_test_hook or artifact_hook != args.agent_test_hook:
        raise SystemExit('Agent-test variant does not match the build manifest/binary; rebuild first')
    if not artifact_hook:
        subprocess.run(['sh', str(ROOT/'port/tests/agent_test_hook_absent.sh'),
                        str(BUILD/'MeleeUnlocked')], check=True)
    for needed in [GAME_LIBRARY, GAME_LIBRARY.with_suffix('.snapexcl'), GAME_ASSETS/'Sys/GC/dsp_coef.bin']:
        if not needed.is_file():
            raise SystemExit(f'Missing native build input: {needed}')
    if APP.exists():
        shutil.rmtree(APP)
    contents = APP/'Contents'
    res = contents/'Resources'
    copy(BUILD/'MeleeUnlocked', contents/'MacOS/MeleeUnlocked')
    copy(ROOT.parent/'deps/sdl2/lib/libSDL2-2.0.0.dylib', contents/'Frameworks/libSDL2-2.0.0.dylib')
    copy(GAME_LIBRARY,res/'melee_game.so')
    if (ROOT/'spike/shader-seed').is_dir():
        copy(ROOT/'spike/shader-seed',res/'shader-seed')
    copy(GAME_LIBRARY.with_suffix('.snapexcl'),res/'melee_game.snapexcl')
    for tool in ['glslangValidator','spirv-cross']:
        # copy2 follows the glslang symlink (equivalent to cp -L).
        copy(ROOT.parent/'deps/tools/bin'/tool,res/'bin'/tool)
    # Linux release's runtime asset list, mapped to Resources. All mutable state lives in --user-dir.
    for src, dst in [('port/slippi_sys','Sys'), ('port/slippi_sys_playback','SysPlayback'),
                     ('port/slippi_sys_general','slippi_sys_general'),('lang','lang')]:
        copy(ROOT/src, res/dst)
    shutil.copytree(GAME_ASSETS/'Sys',res/'Sys',dirs_exist_ok=True,symlinks=False)
    copy(GAME_ASSETS/'Sys/GC/dsp_coef.bin',res/'Sys/GC/dsp_coef.bin')
    copy(GAME_ASSETS/'Sys/GC/dsp_coef.bin',res/'SysPlayback/GC/dsp_coef.bin')
    # Two of Slippi's Sys files are mostly Nintendo's own disc assets (compliance-lead, 10-25): they never ship. The
    # host rebuilds them from the player's disc (host/sysgen.cpp reads Resources/deltas, next to Sys).
    deltas = json.loads((ROOT/'port/sys_deltas/manifest.json').read_text())
    disc_derived = {f['target'] for f in deltas['files']}
    for folder in ['Sys','SysPlayback','slippi_sys_general']:
        for name in disc_derived:
            (res/folder/'GameFiles/GALE01'/name).unlink(missing_ok=True)
    (res/'deltas').mkdir()
    copy(ROOT/'port/sys_deltas/manifest.json',res/'deltas/manifest.json')
    for f in deltas['files']:
        copy(ROOT/'port/sys_deltas'/f['delta'],res/'deltas'/f['delta'])
    packaging = runpy.run_path(str(ROOT/'tools/package_linux_release.py'),run_name='mac_pack')
    (res/'Shaders').mkdir()
    (res/'Shaders/crt-shaders.pak').write_bytes(packaging['shader_pak']())
    copy(ROOT/'port/app/bailey_cut.bmp',res/'ui/bailey.bmp')
    for source in ['gd_melee','mockups','menu_previews','vendor']:
        copy(ROOT/'port/runtime/gx/ui_sources'/source/'assets',res/'ui_sources'/source)
    copy(ROOT/'docs/training-mods.md',res/'TrainingMods.md')
    # Keep the same notices as the Linux packaging script, without executing Linux packaging.
    import ast
    tree = ast.parse((ROOT/'tools/package_linux_release.py').read_text())
    for node in ast.walk(tree):
        if isinstance(node,ast.For) and isinstance(node.target,ast.Tuple) and [n.id for n in node.target.elts if isinstance(n,ast.Name)] == ['src','dst']:
            def rooted_path(expression):
                if isinstance(expression, ast.Name) and expression.id == 'ROOT':
                    return ROOT
                if isinstance(expression, ast.BinOp) and isinstance(expression.op, ast.Div):
                    return rooted_path(expression.left) / ast.literal_eval(expression.right)
                raise SystemExit('Unsupported source in Linux release license list')
            for pair in node.iter.elts:
                copy(rooted_path(pair.elts[0]), res/'licenses'/ast.literal_eval(pair.elts[1]))
    (res/'EXPERIMENTAL.txt').write_text('M7 0.9.56 full macOS host spike; unsigned. See port/mac/README.md for measured limitations.\n')
    staging = 'STAGING ONLY: STAGING AGENT-TEST BUILD. NOT FOR PRODUCTION\n\n' if artifact_hook else ''
    (res/'README.txt').write_text(staging + 'Melee Unlocked macOS experimental build; unsigned.\n', encoding='utf-8')
    (res/'build-manifest.json').write_text(json.dumps(built, indent=2)+'\n')
    version = (ROOT/'VERSION').read_text().strip()
    info = dict(CFBundleIdentifier='co.yougame.meleeunlocked', CFBundleName='Melee Unlocked',
                CFBundleDisplayName='Melee Unlocked - Experimental', CFBundleExecutable='MeleeUnlocked',
                CFBundlePackageType='APPL',CFBundleShortVersionString=version, CFBundleVersion=version,
                LSMinimumSystemVersion='15.0',LSArchitecturePriority=['x86_64'],
                NSHighResolutionCapable=True,NSPrincipalClass='NSApplication')
    with (contents/'Info.plist').open('wb') as f:
        plistlib.dump(info,f)
    assert not any(p.is_symlink() for p in APP.rglob('*')), 'Bundle contains symlinks'
    shipped = sorted(str(p.relative_to(APP)) for p in APP.rglob('*') if p.name in disc_derived)
    assert not shipped, f'Bundle carries disc-derived Sys files: {shipped}'
    subprocess.run(['/usr/bin/otool','-L',str(contents/'MacOS/MeleeUnlocked')],check=True)
    print('Unsigned bundle:',APP)
if __name__ == '__main__':
    main()
