#!/usr/bin/env python3
"""M4, informational: phase-tolerant loudness comparison of the scripted-hit window.

The M4 gate compares the single loudest sample of the hit window (12.8% apart) while RMS is
0.02% apart; the difference is two voices starting one 5 ms AX frame apart (see
MILESTONE_STATUS.md, "M4 scripted-hit peak"). This reports what the owner would be choosing
between: the peak of the short-window RMS envelope at several window lengths, the envelope's
largest per-window difference, and the raw peak with the native mix shifted by up to one AX
frame. Windows are aligned on the first match frame exactly as tools/audio_event_compare.py does.
Nothing here changes a gate.

    python tools/m4_phase_tolerant.py run-source/<pair-dir> [--json out.json]
"""
import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from audio_event_compare import boundaries, pcm  # noqa: E402

AX_FRAME = 160   # samples per 5 ms AX frame at 32 kHz


def mono(samples):
    return [(samples[i] + samples[i + 1]) / 2.0 for i in range(0, len(samples) - 1, 2)]


def envelope(x, width):
    out, acc = [], 0.0
    sq = [v * v for v in x]
    for i, v in enumerate(sq):
        acc += v
        if i >= width:
            acc -= sq[i - width]
        if i >= width - 1:
            out.append(math.sqrt(acc / width))
    return out


def pct(a, b):
    return abs(a - b) * 100.0 / abs(b) if b else (0.0 if a == 0 else 100.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pair', type=Path)
    ap.add_argument('--window', nargs=2, type=float, default=(1000.0, 1400.0))
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    sig = {}
    for label, sub in (('native', 'native'), ('reference', 'translated')):
        _, match = boundaries(a.pair / sub / 'state.csv')
        rate, s = pcm(a.pair / sub / 'audio.wav')
        m = mono(s)
        first = round((match + a.window[0]) * rate / 60)
        last = round((match + a.window[1]) * rate / 60)
        sig[label] = (m, first, last)
    n_m, n0, n1 = sig['native']
    r_m, r0, r1 = sig['reference']
    nat, ref = n_m[n0:n1], r_m[r0:r1]
    report = {'window_match_retraces': list(a.window), 'samples': len(ref)}
    report['raw_peak_difference_percent'] = pct(max(map(abs, nat)), max(map(abs, ref)))
    report['rms_difference_percent'] = pct(math.sqrt(sum(v * v for v in nat) / len(nat)),
                                           math.sqrt(sum(v * v for v in ref) / len(ref)))
    env = {}
    for ms in (5, 10, 20, 50):
        w = ms * 32
        en, er = envelope(nat, w), envelope(ref, w)
        k = min(len(en), len(er))
        env['%dms' % ms] = {
            'envelope_peak_difference_percent': pct(max(en[:k]), max(er[:k])),
            'largest_pointwise_difference_percent_of_reference_peak':
                max(abs(en[i] - er[i]) for i in range(k)) * 100.0 / max(er[:k]),
        }
    report['rms_envelope'] = env
    best = None
    for shift in range(-AX_FRAME, AX_FRAME + 1, 8):
        seg = n_m[n0 + shift:n1 + shift]
        d = pct(max(map(abs, seg)), max(map(abs, ref)))
        if best is None or d < best[1]:
            best = (shift, d)
    report['raw_peak_best_whole_mix_shift_within_one_ax_frame'] = {'shift_samples': best[0],
                                                                    'difference_percent': best[1]}
    print(json.dumps(report, indent=1))
    if a.json:
        a.json.write_text(json.dumps(report, indent=1) + '\n')


if __name__ == '__main__':
    main()
