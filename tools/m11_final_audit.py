#!/usr/bin/env python3
"""M11: run every automated milestone check against the current build in one pass. Nothing is published.

Checks (each hidden and muted, in its own directory under --out):
  ctest          native and host Release suites
  m5_sweeps      26 characters and 27 stages, 2,400 retraces each (tools/sweep_windows.py)
  m7             four scripted scenarios (original, items off, Onett, Jungle Japes) against build-vanilla,
                 each with at least 3,600 exact match frames
  m9_codes       General Codes + Lagless FoD against build-general: the random-stage and FoD seeds exact for the
                 whole compared match, every UCF path inside an exact window, Salty Runback matches exact,
                 L+R+A+Start and the debug-menu route in identical scene order
  m4_voices      per-voice audio standard (tools/m4_voice_compare.py) on the scripted pair
  m5_fuzz        seeded --match fuzz runs over all characters, codes on and off, finish without a crash
Owner sign-offs (M6 visible checks, M8 feel) are read from --signoff when given and otherwise reported as
awaiting the owner; this tool never marks them.

    python tools/m11_final_audit.py --out run-source/m11-final-audit-YYYYMMDD [--jobs 3] [--skip m5_fuzz]
"""
import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISO = r'C:\Games\Smash\DOLPHIN AND SMASH GAMES\Super Smash Bros. Melee (v1.02).iso'
SRC = ROOT / 'build-sourceport/port/Release/melee_source.exe'
VAN = ROOT / 'build-vanilla/port/Release/melee_port.exe'
GEN = ROOT / 'build-general/port/Release/melee_port.exe'
PS = ROOT / 'port/scripts'
EVIDENCE = ROOT / 'run-source'
CODES = EVIDENCE / 'm9-gecko-scripts-20260924'
UCF = EVIDENCE / 'gecko-scripts-20260924'
FEAT = EVIDENCE / 'final-ucf-scripts-20260925'
FUZZ = EVIDENCE / 'fuzzB-scripts-20260924'
SWEEP_REF = EVIDENCE / 'coord-sweeps-20260924'
MIN_FREE = 15 * 1024 ** 3
PY = sys.executable
UCF_KINDS = ('shield-drop', 'shield-drop-extended', 'squatrv', 'dashback', 'sdi', 'shield-sdi', 'tumble')


def env(**extra):
    e = {k: v for k, v in os.environ.items()
         if k not in ('MELEE_TRACE_GECKO', 'MELEE_TRACE_AX_VOICES', 'MELEE_TEST_EARLY_RNG_SEED')}
    e['MELEE_NO_GC_ADAPTER'] = '1'
    e.update(extra)
    return e


def run(cmd, log, **kw):
    with open(log, 'w') as f:
        return subprocess.run([str(c) for c in cmd], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, **kw).returncode


def compare(pair):
    out = subprocess.run([PY, 'tools/m9_pair_compare.py', str(pair)], cwd=ROOT, capture_output=True, text=True).stdout
    return json.loads(out)


def pair(out, script, frames, game, **env_extra):
    cmd = [PY, 'tools/run_lockstep_pair.py', '--native-exe', SRC, '--translated-exe', GEN if game == 'general' else VAN,
           '--iso', ISO, '--script', script, '--out', out, '--frames', frames, '--native-game', game]
    if game == 'general':
        cmd += ['--sys-dir', ROOT / 'port/slippi_sys_general']
    run(cmd, str(out) + '.log', env=env(**env_extra))
    return compare(out)


def segments(state):
    segs, cur, last = [], {}, 0
    for r in csv.DictReader(open(state)):
        mf = int(r['match_frame'], 16)
        if not mf:
            continue
        if mf < last and cur:
            segs.append(cur)
            cur = {}
        last = mf
        cur.setdefault(mf, {k: v for k, v in r.items() if k not in ('frame', 'retrace')})
    if cur:
        segs.append(cur)
    return segs


def ucf_events(log):
    ev = []
    for line in Path(log).read_text(errors='replace').splitlines():
        if '[gecko] ' not in line:
            continue
        parts = line.split('[gecko] ')[1].split()
        kv = dict(p.split('=', 1) for p in parts[1:] if '=' in p)
        ev.append((parts[0], int(kv.get('frame', '0'))))
    return ev


def check_ctest(o):
    n = run(['ctest', '--test-dir', ROOT / 'build-sourceport-gcc'], o / 'ctest-native.log')
    h = run(['ctest', '--test-dir', ROOT / 'build-sourceport', '-C', 'Release'], o / 'ctest-host.log')
    return dict(passed=n == 0 and h == 0, native_exit=n, host_exit=h)


def check_sweeps(o):
    res = {}
    for name in ('characters', 'stages'):
        specs = [l.split()[1] for l in (SWEEP_REF / f'{name}.txt').read_text().splitlines() if l.strip()]
        run([PY, 'tools/sweep_windows.py', '--out', o / f'sweep-{name}'] + specs, o / f'sweep-{name}.log', env=env())
        lines = (o / f'sweep-{name}.log').read_text(errors='replace').splitlines()
        res[name] = dict(expected=len(specs), passed=sum(l.startswith('passed') for l in lines))
    return dict(passed=all(v['passed'] == v['expected'] for v in res.values()), **res)


def check_m7(o):
    scen = {'original': 'parity_vs.txt', 'items_off': 'parity_vs_items_off.txt',
            'onett': 'parity_vs_onett.txt', 'japes': 'parity_vs_west.txt'}
    res = {}
    for k, s in scen.items():
        c = pair(o / f'm7-{k}', PS / s, 6500, 'vanilla')
        res[k] = dict(exact=c['exact_prefix'], first_divergence=c['first_divergence'])
    return dict(passed=all(v['exact'] >= 3600 for v in res.values()), **res)


def check_codes(o):
    res = {}
    for seed in ('s2-random-stage', 's4-random-stage', 's5-random-stage', 's7-random-stage', 's8-fod'):
        c = pair(o / f'codes-{seed}', CODES / f'fuzz-{seed}.txt', 9000 if 'fod' in seed else 8000, 'general')
        res[seed] = dict(exact=c['exact_prefix'], compared=c['translated_match_frames'], scenes_equal=c['scenes_equal'],
                         whole=c['exact_prefix'] == c['translated_match_frames'] > 0 and c['scenes_equal'])
    fired = {}
    for name, script in (('ucf-a', UCF / 'ucf-a-sdrop-dashback-squatrv-sdi-shieldsdi.txt'),
                         ('ucf-b', UCF / 'ucf-b-tumble.txt'), ('ucf-c', FEAT / 'ucf-c-shieldsdi.txt')):
        c = pair(o / name, script, 2500, 'general', MELEE_TRACE_GECKO='1')
        for kind, frame in ucf_events(o / name / 'native' / 'game.log'):
            if frame <= c['exact_prefix']:
                fired[kind] = fired.get(kind, 0) + 1
        res[name] = dict(exact=c['exact_prefix'])
    res['ucf_in_exact_windows'] = fired
    run([PY, 'tools/run_lockstep_pair.py', '--native-exe', SRC, '--translated-exe', GEN, '--iso', ISO, '--script',
         FEAT / 'feat-runback-rt.txt', '--out', o / 'runback', '--frames', 3000, '--native-game', 'general',
         '--sys-dir', ROOT / 'port/slippi_sys_general'], o / 'runback.log', env=env())
    n, t = segments(o / 'runback/native/state.csv'), segments(o / 'runback/translated/state.csv')
    common = min(len(n), len(t))
    res['runback'] = dict(matches_compared=common, exact=all(
        all(n[i][mf] == t[i].get(mf) for mf in n[i] if mf in t[i]) for i in range(common)))
    for name in ('lras-rt', 'debug'):
        c = pair(o / name, FEAT / f'feat-{name}.txt', 3000, 'general')
        res[name] = dict(exact=c['exact_prefix'], scenes_equal=c['scenes_equal'])
    ok = (all(res[s]['whole'] for s in ('s2-random-stage', 's4-random-stage', 's5-random-stage', 's7-random-stage',
                                          's8-fod'))
          and all(fired.get(k) for k in UCF_KINDS) and res['runback']['exact'] and res['runback']['matches_compared'] >= 2
          and res['lras-rt']['scenes_equal'] and res['debug']['scenes_equal'])
    return dict(passed=ok, **res)


def check_m4(o):
    out = o / 'm4-voices'
    run([PY, 'tools/run_lockstep_pair.py', '--native-exe', SRC, '--translated-exe', VAN, '--iso', ISO, '--script',
         PS / 'parity_vs.txt', '--out', out, '--frames', 3200, '--audio-dump', '--native-game', 'vanilla'],
        o / 'm4-voices.log', env=env(MELEE_TRACE_AX_VOICES='1', MELEE_TEST_EARLY_RNG_SEED='0x12345678@300'))
    run([PY, 'tools/m4_voice_compare.py', '--run', out, '--json', out / 'voice-compare.json', '--table',
         out / 'voice-table.txt'], o / 'm4-compare.log')
    d = json.loads((out / 'voice-compare.json').read_text())
    counts = {k: v['condition_counts_pass_fail'] for k, v in d['windows'].items()}
    unmatched = sum(len(v['unmatched_native']) + len(v['unmatched_legacy']) for v in d['windows'].values())
    c2 = all(v['c2_samples'][1] == 0 for v in counts.values())
    c3 = all(v['c3_cause_aligned'][1] == 0 and v['c3_model_native'][1] == 0 and v['c3_model_legacy'][1] == 0
             for v in counts.values())
    c1_fail = [(k, x) for k, v in d['windows'].items() for x in v['voices']
               if isinstance(x, dict) and 'c1_params' in (x.get('failed') or [])]
    c1_ok = all(k == 'menu_music' and all(f['field'].startswith('adpcm_loop.') for f in x.get('c1_differences', []))
                for k, x in c1_fail)
    return dict(passed=unmatched == 0 and c2 and c3 and c1_ok, counts=counts, unmatched=unmatched,
                c1_exceptions=len(c1_fail))


def check_fuzz(o, jobs):
    runs = []
    chars = sorted({int(l.split()[1].split(':')[1]) for l in (SWEEP_REF / 'characters.txt').read_text().splitlines()
                    if l.strip()})
    stages = [l.split()[1].split(':')[0] for l in (SWEEP_REF / 'stages.txt').read_text().splitlines() if l.strip()]
    for i, c in enumerate(chars):
        opp = chars[(i * 7 + 3) % 26]
        opp = chars[(i + 1) % 26] if opp == c else opp
        for vanilla in (True, False):
            stage = stages[(i * 2 + (0 if vanilla else 13)) % 27]
            seed = (3000 if vanilla else 4000) + i
            script = FUZZ / f"genb-{'van' if vanilla else 'gen'}-s{seed}.txt"
            runs.append((f"{'van' if vanilla else 'gen'}-{c}-vs-{opp}-{stage}", f'{stage}:{c}/c9:{opp}/c9', script, vanilla))

    def one(spec):
        name, match, script, vanilla = spec
        d = o / 'fuzz' / name
        for sub in ('card', 'User', 'Replays'):
            (d / sub).mkdir(parents=True, exist_ok=True)
        (d / 'User' / 'user.json').write_text('{}\n')
        cmd = [SRC, '--iso', ISO, '--hidden', '--volume', '0', '--time-base', '1', '--fast', '--frames', 11000,
               '--script', script, '--match', match, '--replay-dir', d / 'Replays', '--card-dir', d / 'card',
               '--user-dir', d / 'User', '--settings-path', d / 'settings.ini', '--log-file', d / 'game.log']
        if vanilla:
            cmd.append('--vanilla-game')
        try:
            code = run(cmd, d / 'stdout.txt', env=env(), timeout=900)
        except subprocess.TimeoutExpired:
            code = 'timeout'
        log = (d / 'game.log').read_text(errors='replace') if (d / 'game.log').exists() else ''
        ok = code == 0 and 'exit requested after 11000 retraces' in log and 'panic' not in log.lower()
        shutil.rmtree(d / 'shadercache', ignore_errors=True)
        return name, ok

    with ThreadPoolExecutor(jobs) as ex:
        results = dict(ex.map(one, runs))
    return dict(passed=all(results.values()), runs=len(results), failed=[k for k, v in results.items() if not v])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--jobs', type=int, default=3)
    ap.add_argument('--skip', action='append', default=[])
    ap.add_argument('--signoff', type=Path, help='JSON with owner sign-offs, e.g. {"m6_visible": true, "m8_feel": true}')
    a = ap.parse_args()
    o = a.out if a.out.is_absolute() else ROOT / a.out
    if o.exists():
        sys.exit(f'{o} exists; use a fresh directory')
    o.mkdir(parents=True)
    if shutil.disk_usage(str(o)).free < MIN_FREE:
        sys.exit('C: has under 15 GiB free')
    checks = [('ctest', check_ctest), ('m5_sweeps', check_sweeps), ('m7', check_m7), ('m9_codes', check_codes),
              ('m4_voices', check_m4)]
    summary = {}
    for name, fn in checks:
        if name in a.skip:
            continue
        (o / name).mkdir()
        summary[name] = fn(o / name)
        print(name, 'PASS' if summary[name]['passed'] else 'FAIL', flush=True)
        (o / 'summary.json').write_text(json.dumps(summary, indent=1))
    if 'm5_fuzz' not in a.skip:
        (o / 'm5_fuzz').mkdir()
        summary['m5_fuzz'] = check_fuzz(o / 'm5_fuzz', a.jobs)
        print('m5_fuzz', 'PASS' if summary['m5_fuzz']['passed'] else 'FAIL', flush=True)
    signoff = json.loads(a.signoff.read_text()) if a.signoff else {}
    summary['owner'] = {k: signoff.get(k, 'awaiting owner') for k in ('m6_visible', 'm8_feel')}
    summary['all_automated_passed'] = all(v['passed'] for k, v in summary.items() if isinstance(v, dict) and 'passed' in v)
    (o / 'summary.json').write_text(json.dumps(summary, indent=1))
    print(json.dumps({k: (v['passed'] if isinstance(v, dict) and 'passed' in v else v) for k, v in summary.items()}))
    return 0 if summary['all_automated_passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
