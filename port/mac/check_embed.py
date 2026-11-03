"""M5 direct-clang exception: compile/link hooks and run pure tests; never initialize AppKit."""
from pathlib import Path
import concurrent.futures
import json
import subprocess
import sys
sys.dont_write_bytecode = True
from build_full import ROOT, SDL, memory_gate

BUILD = ROOT / 'build-mac-embed'

def main():
    BUILD.mkdir(exist_ok=True)
    home = BUILD / 'home'
    home.mkdir(exist_ok=True)
    environment = {'PATH':'/usr/bin:/bin:/usr/sbin:/sbin', 'LANG':'C', 'LC_ALL':'C', 'HOME':str(home)}
    for key in ['XDG_DATA_HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_STATE_HOME','XDG_RUNTIME_DIR','TMPDIR']:
        folder = BUILD / key.lower()
        folder.mkdir(exist_ok=True)
        environment[key] = str(folder)
    flags = ['-arch','x86_64','-mmacosx-version-min=15.0','-std=c++17','-Wall','-Wextra',
             '-Wno-unknown-pragmas','-Wno-multichar','-Wno-deprecated-declarations','-O1']
    results = []
    def run(label, command, timeout=120):
        memory_gate()
        with (BUILD / (label + '.log')).open('w') as log:
            process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                log.write(f'Killed owned PID {process.pid} after {timeout}s\n')
                code = 124
        result = {'step':label, 'exit':code}
        print(f'{label}: exit {code}', flush=True)
        results.append(result)
        return code
    clang = '/usr/bin/clang++'
    unit = BUILD / 'embed-mac-test'
    if run('unit-build', [clang,*flags,str(ROOT/'port/mac/embed_mac_test.cpp'),'-o',str(unit)]):
        return 1
    if run('unit', [str(unit)], timeout=30):
        print((BUILD/'unit.log').read_text(), flush=True)
        return 1
    print((BUILD/'unit.log').read_text(), flush=True)
    native = BUILD/'embed_mac.mm.o'
    if run('native-build', [clang,*flags,'-fblocks','-fno-objc-arc','-I'+str(SDL/'include'),'-I'+str(SDL/'include/SDL2'),
                            '-c',str(ROOT/'port/mac/embed_mac.mm'),'-o',str(native)]):
        return 1
    # Link the real adapter against SDL and AppKit, without running any Cocoa code.
    stub = BUILD/'link-stub.cpp'
    stub.write_text('#include "yougame_link.h"\n'
        'namespace yougame { void set_box_handler(std::function<void(const std::string&)>) {} }\n'
        'namespace host { void log(const char*, ...) {} }\n'
        'int main() { return 0; }\n')
    if run('native-link', [clang,*flags,'-I'+str(ROOT/'port/runtime/hle'),str(stub),str(native),
        '-L'+str(SDL/'lib'),'-lSDL2-2.0.0','-framework','AppKit','-framework','CoreGraphics',
        '-Wl,-rpath,'+str(SDL/'lib'),'-o',str(BUILD/'embed-link-check')]):
        return 1
    if run('manual-helper-build', [clang,*flags,'-fblocks','-fno-objc-arc',
        str(ROOT/'port/mac/embed_test_parent.mm'),'-framework','AppKit',
        '-o',str(BUILD/'embed-test-parent')]):
        return 1
    includes = ['port/linux/compat','port/linux','port/app','port/runtime/ppc','port/runtime/hle',
                'port/runtime/host','port/runtime/gx','port/runtime/mu_net','port/runtime/abi',
                'port/third_party','native','port/third_party/imgui','port/third_party/imgui/backends']
    shared = [*flags,'-mavx2','-mfma','-include',str(ROOT/'port/linux/compat/windows.h'),
              '-DMELEE_LINUX','-DGX_PC_SETTINGS','-DMELEE_SOURCE_PORT',
              '-DMELEE_PORT_VERSION="embed-test"','-DGX_SHADER_CACHE_VERSION="mac-gl-1"',
              *['-I'+str(ROOT/p) for p in includes],'-I'+str(SDL/'include'),'-I'+str(SDL/'include/SDL2')]
    hooks = ['port/runtime/host/window.cpp','port/linux/full/gx_backend_linux.cpp','port/linux/renderer_gl.cpp']
    # At most two compile jobs, checking memory immediately before each one.
    def compile_hook(source):
        label = Path(source).stem + '-hook'
        return run(label, [clang,*shared,'-c',str(ROOT/source),'-o',str(BUILD/(label+'.o'))])
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for start in range(0,len(hooks),2):
            if any(pool.map(compile_hook, hooks[start:start+2])):
                return 1
    (BUILD/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Embed checks passed; native/SDL link checked without opening windows.', flush=True)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
