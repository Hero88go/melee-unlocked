#!/usr/bin/env python3
"""Clicks, dropouts, holds, repeated blocks and rate steps in an audio capture.

Reads a WAV (16/24/32-bit integer or 32/64-bit float, any rate, any channel count, including files
whose header was never finalized) or headerless PCM (--raw f32le:32000:2). Reports every event with
its time, kind and severity, as JSON and as a short text summary, plus optional PNG plots.

Kinds of event
  click     an impulsive discontinuity: the linear-prediction residual (what a 20 ms, order 24-32
            model of the signal could not predict) jumps to T times its local RMS (default T = 12;
            local RMS over +-5 ms with the centre +-0.5 ms left out), is at least -66 dBFS in
            absolute size and no more than 30 dB below the signal level around it (+-10 ms).
            Steps, spikes, splices (a skipped block) and slope breaks land here.
  cutoff    the same test, where the signal's energy then drops by 12 dB or more (a sound or the
            whole stream stopping abruptly).
  onset     the same test, where the energy rises by 6 dB or more, or the prediction residual
            stays 6 dB higher for the next 3 ms (a new sound entering over other material; a click
            leaves both where they were): a sound starting with a hard attack. Listed, but not
            counted as a defect (Melee has many hard attacks).
  hold      the whole signal (every channel) freezes: consecutive samples exactly equal, or decaying
            geometrically with one constant ratio in every channel (the starvation fade of our own
            output, see port/runtime/host/audio.cpp), right after audible activity.
  dropout   a run of exact digital zeros entered and left abruptly inside audible audio.
  repeat    a 5 ms block (160 frames at 32 kHz, 240 at 48 kHz) repeated exactly, including bursts
            at 2x to 4x that lag. Silent, constant and periodic material is excluded.
  starvation, rate_step   only with --rate-csv (the <dump>.rate.csv written next to a
            MELEE_AUDIO_DEVICE_DUMP capture): every starved device callback, with the audio level
            around it, and every callback-to-callback resampling ratio change above --rate-step-cents.

Clicks and cutoffs within 2 ms of a hold, dropout or repeat are attached to it instead of being
counted twice. The headline "defects" count is clicks + cutoffs + holds + dropouts + repeats.

What it cannot see (measured, see --selftest and run-source/rel085-final/audio-compare/METHOD.md):
  * a hold of one layer of a mix while another keeps playing (our starvation fade happens before the
    jukebox music is mixed in, so under music a 0.1-1 ms SFX hold leaves almost no trace in the
    waveform; the rate CSV counts those exactly);
  * steps smaller than about -35 dBFS inside loud, busy material (the local residual masks them);
  * Melee's own hard transients are real content and are counted like any other click. Compare
    against a reference capture of the same content (tools/audio_compare.py) to separate added
    clicks from native ones.

    py -3.12 tools/audio_clicks.py device.wav --rate-csv device.wav.rate.csv --json out.json --png out.png
    py -3.12 tools/audio_clicks.py dump.f32 --raw f32le:32000:2
    py -3.12 tools/audio_clicks.py --inject-test source.wav     (sensitivity on real audio)
    py -3.12 tools/audio_clicks.py --selftest                     (must print PASS)
"""
import argparse
import csv
import json
import math
import struct
import sys
from fractions import Fraction
from pathlib import Path

import numpy as np

try:
    from scipy.signal import butter, resample_poly, sosfiltfilt
except ImportError:  # pragma: no cover - reported at run time
    butter = resample_poly = sosfiltfilt = None

VERSION = 1
DEFAULTS = dict(threshold=12.0, min_level_db=-66.0, min_rel_db=-30.0, merge_ms=3.0, scale_win_ms=5.0, scale_hole_ms=0.5,
                block_ms=20.0, onset_db=6.0, cutoff_db=12.0, onset_residual_db=6.0, hold_min_ms=0.12, hold_min_samples=6, hold_level_db=-60.0,
                hold_context_ratio=4.0, zero_min_ms=0.5, zero_max_ms=200.0, zero_edge_db=-50.0,
                repeat_level_db=-60.0, rate_step_cents=2.0, attach_ms=2.0)
SWEEP = (8, 10, 12, 15, 20, 30)


# ---------------------------------------------------------------------------------------------
# Reading audio

def parse_raw_spec(spec):
    """'f32le:32000:2' or 's16le:44100:2' -> (numpy dtype, rate, channels)."""
    parts = spec.split(':')
    if len(parts) != 3:
        raise SystemExit(f"--raw wants format:rate:channels, e.g. f32le:32000:2 (got {spec})")
    fmt, rate, channels = parts[0].lower(), int(parts[1]), int(parts[2])
    dtypes = {'f32le': '<f4', 'f64le': '<f8', 's16le': '<i2', 's32le': '<i4', 'f32be': '>f4', 's16be': '>i2'}
    if fmt not in dtypes:
        raise SystemExit(f"--raw format must be one of {', '.join(dtypes)}")
    return np.dtype(dtypes[fmt]), rate, channels


def read_audio(path, raw=None):
    """Returns (rate, samples float64 [frames, channels] in full-scale units, info dict).

    WAV headers written by a process that was killed (Dolphin writes 100,000,000 as a placeholder
    size, our dumps write 0) are handled by taking the data chunk up to the end of the file."""
    path = Path(path)
    size = path.stat().st_size
    info = dict(path=str(path), bytes=size)
    if raw:
        dtype, rate, channels = parse_raw_spec(raw)
        count = size // dtype.itemsize // channels * channels
        data = np.fromfile(path, dtype=dtype, count=count)
        info.update(container='raw', format=raw)
        return rate, _normalise(data, dtype, channels, info), info
    with open(path, 'rb') as f:
        head = f.read(12)
        if len(head) < 12 or head[:4] not in (b'RIFF', b'RF64') or head[8:12] != b'WAVE':
            raise SystemExit(f"{path}: not a WAV file (pass --raw format:rate:channels for headerless PCM)")
        fmt = None
        pos = 12
        while pos + 8 <= size:
            f.seek(pos)
            tag, length = struct.unpack('<4sI', f.read(8))
            body_at = pos + 8
            if tag == b'fmt ':
                body = f.read(min(length, 64))
                tagv, channels, rate = struct.unpack('<HHI', body[:8])
                bits = struct.unpack('<H', body[14:16])[0]
                if tagv == 0xFFFE and len(body) >= 26:
                    tagv = struct.unpack('<H', body[24:26])[0]   # first two bytes of the sub-format GUID
                fmt = (tagv, channels, rate, bits)
            elif tag == b'data':
                if fmt is None:
                    raise SystemExit(f"{path}: data chunk before fmt chunk")
                tagv, channels, rate, bits = fmt
                available = size - body_at
                declared = length
                nbytes = available if declared == 0 or declared > available else declared
                if declared != nbytes:
                    info['header_note'] = f"data chunk declares {declared} bytes, file holds {available}; used {nbytes}"
                if tagv == 3 and bits in (32, 64):
                    dtype = np.dtype('<f4' if bits == 32 else '<f8')
                elif tagv == 1 and bits in (8, 16, 24, 32):
                    dtype = {8: np.dtype('u1'), 16: np.dtype('<i2'), 24: None, 32: np.dtype('<i4')}[bits]
                else:
                    raise SystemExit(f"{path}: unsupported WAV format tag {tagv} with {bits} bits")
                f.seek(body_at)
                if bits == 24:
                    frame_bytes = 3 * channels
                    buf = np.frombuffer(f.read(nbytes // frame_bytes * frame_bytes), dtype=np.uint8)
                    b3 = buf.reshape(-1, 3).astype(np.int32)
                    data = (b3[:, 0] | (b3[:, 1] << 8) | (b3[:, 2] << 16))
                    data = np.where(data >= 1 << 23, data - (1 << 24), data)
                    info.update(container='wav', format='int24')
                    x = (data.astype(np.float64) / 8388608.0).reshape(-1, channels)
                    info.update(rate=rate, channels=channels, integer=True, lsb=1 / 8388608.0)
                    return rate, x, info
                count = nbytes // dtype.itemsize // channels * channels
                data = np.fromfile(f, dtype=dtype, count=count)
                info.update(container='wav', format={np.dtype('<f4'): 'float32', np.dtype('<f8'): 'float64',
                                                     np.dtype('<i2'): 'int16', np.dtype('<i4'): 'int32',
                                                     np.dtype('u1'): 'uint8'}[dtype])
                return rate, _normalise(data, dtype, channels, info), info
            pos = body_at + length + (length & 1)
            if length == 0 and tag != b'data':
                pos = body_at
                if pos + 8 > size:
                    break
    raise SystemExit(f"{path}: no data chunk")


def _normalise(data, dtype, channels, info):
    kind = dtype.kind
    if kind == 'f':
        x = data.astype(np.float64)
        info.update(integer=False, lsb=0.0)
    elif kind == 'u':
        x = (data.astype(np.float64) - 128.0) / 128.0
        info.update(integer=True, lsb=1 / 128.0)
    else:
        full = float(2 ** (8 * dtype.itemsize - 1))
        x = data.astype(np.float64) / full
        info.update(integer=True, lsb=1 / full)
    info['channels'] = channels
    return x.reshape(-1, channels)


def db(v, floor=1e-12):
    return 20.0 * math.log10(max(float(v), floor))


# ---------------------------------------------------------------------------------------------
# Signal helpers

class Sums:
    """Window sums of one series via a single cumulative sum."""

    def __init__(self, v):
        self.n = len(v)
        self.c = np.concatenate([[0.0], np.cumsum(v, dtype=np.float64)])

    def mean(self, lo, hi):
        lo = min(max(int(lo), 0), self.n)
        hi = min(max(int(hi), 0), self.n)
        return (self.c[hi] - self.c[lo]) / (hi - lo) if hi > lo else 0.0

    def rms(self, lo, hi):
        return math.sqrt(max(self.mean(lo, hi), 0.0))


def centred_sums(v, half):
    """Sum of v over [i-half, i+half] for every i, and the number of samples inside the file."""
    n = len(v)
    c = np.concatenate([[0.0], np.cumsum(np.concatenate([np.zeros(half), v, np.zeros(half + 1)]))])
    s = c[2 * half + 1:2 * half + 1 + n] - c[:n]
    i = np.arange(n, dtype=np.float64)
    cnt = np.minimum(i + half + 1, n) - np.maximum(i - half, 0)
    return s, cnt


def holed_rms(e, rate, win_ms, hole_ms):
    """RMS of e over +-win_ms around each sample, leaving out +-hole_ms (so a click does not
    inflate its own reference)."""
    w = max(2, int(round(rate * win_ms / 1000.0)))
    h = max(1, int(round(rate * hole_ms / 1000.0)))
    e2 = e * e
    s1, n1 = centred_sums(e2, w)
    s2, n2 = centred_sums(e2, h)
    return np.sqrt(np.maximum(s1 - s2, 0.0) / np.maximum(n1 - n2, 1.0))


def levinson_batch(r, order):
    """Levinson-Durbin for many autocorrelation rows at once. r: (m, order+1)."""
    m = r.shape[0]
    a = np.zeros((m, order + 1))
    a[:, 0] = 1.0
    err = r[:, 0].copy()
    for i in range(1, order + 1):
        acc = r[:, i] + np.einsum('ij,ij->i', a[:, 1:i], r[:, i - 1:0:-1])
        ok = err > 1e-30
        k = np.where(ok, -acc / np.where(ok, err, 1.0), 0.0)
        k = np.clip(k, -0.9999, 0.9999)
        rev = a[:, i - 1:0:-1].copy()
        a[:, 1:i] += k[:, None] * rev
        a[:, i] = k
        err = err * (1.0 - k * k)
    return a


def lpc_residual(x, rate, block_ms=20.0, order=None):
    """Prediction residual of one channel with a block-wise all-pole model (20 ms blocks, each
    fitted on the block plus half a block either side with a Hann window)."""
    n = len(x)
    if order is None:
        order = 32 if rate > 40000 else 24
    B = max(order * 4, int(round(rate * block_ms / 1000.0)))
    nb = (n + B - 1) // B
    W = 2 * B
    pad = np.concatenate([np.zeros(B // 2 + order), x, np.zeros(B + B // 2 + order)])
    win = np.hanning(W)
    lagwin = np.exp(-0.5 * (2 * np.pi * 40.0 * np.arange(order + 1) / rate) ** 2)
    e = np.zeros(n)
    chunk = 512
    nfft = 1 << int(math.ceil(math.log2(2 * W)))
    for c0 in range(0, nb, chunk):
        c1 = min(nb, c0 + chunk)
        starts = np.arange(c0, c1) * B + order      # index in pad of (block start - B//2)
        frames = np.lib.stride_tricks.sliding_window_view(pad, W)[starts] * win
        spec = np.fft.rfft(frames, nfft)
        r = np.fft.irfft(spec.real ** 2 + spec.imag ** 2, nfft)[:, :order + 1]
        r[:, 0] = r[:, 0] * (1.0 + 1e-9) + 1e-12 * W
        r *= lagwin
        a = levinson_batch(r, order)
        for j, b in enumerate(range(c0, c1)):
            s = b * B
            t = min(n, s + B)
            if s >= n:
                break
            seg = pad[s + B // 2: s + B // 2 + order + (t - s)]    # x[s-order : t]
            e[s:t] = np.convolve(seg, a[j], mode='valid')
    return e


def highpass(x, rate, hz=40.0):
    if sosfiltfilt is None or len(x) < 64:
        return x - x.mean(axis=0)
    sos = butter(2, hz, btype='highpass', fs=rate, output='sos')
    return sosfiltfilt(sos, x, axis=0)


def run_bounds(mask):
    """Start/end (exclusive) of each run of True in a boolean array."""
    if not len(mask):
        return np.zeros(0, int), np.zeros(0, int)
    m = np.concatenate([[False], mask, [False]]).astype(np.int8)
    d = np.diff(m)
    return np.flatnonzero(d == 1), np.flatnonzero(d == -1)


# ---------------------------------------------------------------------------------------------
# Detectors

def detect_clicks(x, rate, p, info):
    """Click / cutoff / onset events from the linear-prediction residual."""
    n, ch = x.shape
    min_abs = 10 ** (p['min_level_db'] / 20.0)
    floor = max(info.get('lsb', 0.0), 1e-7)
    zmax = np.zeros(n)
    emax = np.zeros(n)
    zch = np.zeros(n, np.int8)
    res2 = np.zeros(n)
    for c in range(ch):
        e = lpc_residual(x[:, c], rate, p['block_ms'])
        s = holed_rms(e, rate, p['scale_win_ms'], p['scale_hole_ms'])
        z = np.abs(e) / np.maximum(s, floor)
        z[np.abs(e) < min_abs] = 0.0
        better = z > zmax
        zmax[better] = z[better]
        emax[better] = np.abs(e[better])
        zch[better] = c
        np.maximum(res2, e * e, out=res2)
    res_sums = Sums(res2)
    del res2
    lowest = min(min(SWEEP), p['threshold'])
    cand = np.flatnonzero(zmax >= lowest)
    events = []
    if len(cand):
        gap = int(round(rate * p['merge_ms'] / 1000.0))
        splits = np.flatnonzero(np.diff(cand) > gap) + 1
        for grp in np.split(cand, splits):
            k = grp[np.argmax(zmax[grp])]
            events.append(dict(sample=int(k), first=int(grp[0]), last=int(grp[-1]), z=float(zmax[k]),
                               size=float(emax[k]), channel=int(zch[k])))
    # energy before / after, and the local level, on the high-passed mix of all channels
    hp = highpass(x, rate)
    hp_sums = Sums((hp * hp).mean(axis=1))
    del hp
    raw_sums = Sums((x * x).mean(axis=1))
    g = int(round(rate * 0.001))
    w = int(round(rate * 0.006))
    lw = int(round(rate * 0.010))
    rg = max(2, int(round(rate * 0.0003)))
    rw = int(round(rate * 0.003))
    out = []
    sweep = {t: 0 for t in SWEEP}
    for ev in events:
        k = ev['sample']
        pre_rms = hp_sums.rms(k - w, k - g)
        post_rms = hp_sums.rms(k + g, k + w)
        local = raw_sums.rms(k - lw, k + lw)
        ratio = db(post_rms, 1e-9) - db(pre_rms, 1e-9)
        # A click leaves the prediction residual where it was; a new sound entering over other
        # material keeps it raised for milliseconds after its first sample.
        # (the reference ends 1 ms early: a new sound's attack often starts a little before its
        # largest residual sample)
        res_change = db(res_sums.rms(ev['last'] + rg, ev['last'] + rw), 1e-9) - db(res_sums.rms(ev['first'] - rw - g, ev['first'] - g), 1e-9)
        kind = 'click'
        if ratio >= p['onset_db'] and post_rms >= min_abs:
            kind = 'onset'
        elif ratio <= -p['cutoff_db'] and pre_rms >= min_abs:
            kind = 'cutoff'
        elif res_change >= p['onset_residual_db']:
            kind = 'onset'
        # far below the surrounding level (a curvature kink 30 dB under a tone, say): not counted
        if db(ev['size']) - db(local) < p['min_rel_db'] and kind != 'onset':
            continue
        if kind != 'onset':
            for t in SWEEP:
                if ev['z'] >= t:
                    sweep[t] += 1
        if ev['z'] < p['threshold']:
            continue
        out.append(dict(kind=kind, sample=k, t=k / rate, z=round(ev['z'], 1), z_db=round(db(ev['z']), 1),
                        size_dbfs=round(db(ev['size']), 1), local_dbfs=round(db(local), 1),
                        rel_db=round(db(ev['size']) - db(local), 1), energy_change_db=round(ratio, 1),
                        residual_change_db=round(res_change, 1),
                        channel=ev['channel'], span_ms=round((ev['last'] - ev['first'] + 1) * 1000.0 / rate, 2)))
    return out, sweep


def verify_recurrence(seg, lsb, level):
    """seg: samples of a candidate hold including the last live sample first. Returns (f, j0, j1)
    for the longest stretch of steps j0..j1-1 obeying y[k+1] = f*y[k] (every channel, one f), or None.
    Decaying stretches must start at 256 LSB or more (quantisation hides the shape below that)."""
    prev, cur = seg[:-1], seg[1:]
    same = np.all(cur == prev, axis=1)
    if same.all():
        return 1.0, 0, len(cur)
    s0, s1 = run_bounds(same)
    if len(s0) and (s1 - s0).max() * 2 >= len(cur):       # mostly an exact hold
        k = int(np.argmax(s1 - s0))
        return 1.0, int(s0[k]), int(s1[k])
    mag = np.abs(prev)
    strong = mag >= max(64 * lsb, 1e-4)
    if not strong.any():
        return None
    f = float(np.median(cur[strong] / prev[strong]))
    if not 0.90 <= f <= 0.9995:
        return None
    tol = 1.2 * lsb if lsb > 0 else 1e-6
    ok = np.all(np.abs(cur - f * prev) <= tol + 1e-6 * mag, axis=1)
    starts, ends = run_bounds(ok)
    if not len(starts):
        return None
    k = int(np.argmax(ends - starts))
    j0, j1 = int(starts[k]), int(ends[k])
    if np.abs(prev[j0]).max() < max(256 * lsb, level):
        return None
    return f, j0, j1


def detect_holds(x, rate, p, info):
    """Runs where every channel freezes (exact equality) or decays with one constant ratio."""
    n, ch = x.shape
    if n < 16:
        return []
    lsb = info.get('lsb', 0.0)
    level = 10 ** (p['hold_level_db'] / 20.0)
    prev, cur = x[:-1], x[1:]
    exact = np.all(cur == prev, axis=1)
    big = np.abs(prev).max(axis=1)
    ref_c = np.abs(prev).argmax(axis=1)
    rows = np.arange(n - 1)
    with np.errstate(divide='ignore', invalid='ignore'):
        ratio = np.where(prev != 0, cur / prev, np.nan)
    r_ref = ratio[rows, ref_c]
    tol = 2.5 * lsb / np.maximum(big, 1e-12) + 2e-4
    decay = (r_ref >= 0.90) & (r_ref <= 0.9995)
    for c in range(ch):
        zero_c = (prev[:, c] == 0) & (cur[:, c] == 0)
        tol_c = 2.5 * lsb / np.maximum(np.abs(prev[:, c]), 1e-12) + 2e-4
        agree = np.abs(ratio[:, c] - r_ref) <= (tol_c + tol)
        decay &= zero_c | agree | (np.abs(prev[:, c]) < 4 * max(lsb, 1e-6))
    steady = np.zeros(n - 1, bool)
    steady[1:] = np.abs(r_ref[1:] - r_ref[:-1]) <= (tol[1:] + tol[:-1])
    decay &= steady | np.roll(steady, -1)
    held = (exact & (big > 0)) | (decay & (big >= level))
    starts, ends = run_bounds(held)
    min_len = max(p['hold_min_samples'], int(round(rate * p['hold_min_ms'] / 1000.0)))
    d1 = np.abs(np.diff(x, axis=0)).max(axis=1)          # activity: largest first difference
    act_min = max(2 * lsb, 1e-4)
    zero_all = np.all(x == 0, axis=1)
    out = []
    ctx = int(round(rate * 0.002))
    for s, e in zip(starts, ends):
        # transitions s..e-1 are held: samples s+1..e are the frozen stretch (x[s] = last live sample)
        if e - s < min_len:
            continue
        peak = np.abs(x[s:e + 1]).max()
        if peak < level:
            continue
        # The ratio test above is only a prefilter. A frozen stream obeys y[k+1] = f * y[k] with one
        # f for every step and channel (f = 1 for an exact hold), to within the rounding of the
        # format; a tone near its peak drifts away from any single f within a few samples.
        fit = verify_recurrence(x[s:e + 1], lsb, level)
        if fit is None:
            continue
        f_fit, j0, j1 = fit
        if j1 - j0 < min_len:
            continue
        s, e = s + j0, s + j1
        if s < 2:
            continue
        a = max(0, s - ctx)
        before = d1[a:s]
        act_before = float(np.sqrt((before ** 2).mean()))
        act_in = float(np.sqrt((d1[s:e] ** 2).mean()))
        if act_before < act_min or act_before < p['hold_context_ratio'] * act_in:
            continue
        # A freeze cuts off a moving waveform: the slope arriving at the last live sample is much
        # larger than the first held step. A waveform gliding into a peak or through a smooth
        # release arrives slowly, and is not a hold.
        entry = float(np.abs(x[s] - x[s - 1]).max())
        first = float(np.abs(x[s + 1] - x[s]).max())
        if entry < max(4 * lsb, 1e-4) or entry < 3.0 * first:
            continue
        # ...and that slope is ordinary for the waveform before it (a spike just before a smooth
        # stretch is not a moving waveform)
        typical = float(np.median(d1[max(0, s - 1 - ctx // 2):s - 1])) if s - 1 > max(0, s - 1 - ctx // 2) else 0.0
        if entry > 8.0 * max(typical, lsb, 1e-9):
            continue
        # continue the same recurrence down through the last steps of an integer fade, then zeros
        end = e + 1
        lim = min(n, end + int(rate * 0.1))
        tol_ext = 1.5 * lsb if lsb > 0 else 1e-6
        while end < lim and not zero_all[end]:
            pv, cv = x[end - 1], x[end]
            if np.all(np.abs(cv - f_fit * pv) <= tol_ext + 1e-6 * np.abs(pv)):
                end += 1
            else:
                break
        zero_tail = 0
        if end < n and zero_all[end]:
            zs = end
            lim = min(n, end + int(rate * 2.0))
            while end < lim and zero_all[end]:
                end += 1
            zero_tail = end - zs
        resumed = bool(end < n and np.abs(x[end:end + int(rate * 0.05)]).max() > level)
        frames = end - s - 1 - zero_tail
        out.append(dict(kind='hold', sample=int(s + 1), t=(s + 1) / rate, duration_ms=round(frames * 1000.0 / rate, 3),
                        frames=int(frames), zero_tail_ms=round(zero_tail * 1000.0 / rate, 3),
                        held_dbfs=round(db(np.abs(x[s + 1]).max()), 1), ratio=round(f_fit, 5),
                        exact=bool(f_fit == 1.0), context_dbfs=round(db(np.sqrt((x[max(0, s - 4 * ctx):s] ** 2).mean())), 1),
                        resumed=resumed))
    return out


def detect_zero_runs(x, rate, p, info):
    """Runs of exact digital zero, entered and left abruptly, inside audible audio."""
    n, ch = x.shape
    zero = np.all(x == 0, axis=1)
    starts, ends = run_bounds(zero)
    lo = int(round(rate * p['zero_min_ms'] / 1000.0))
    hi = int(round(rate * p['zero_max_ms'] / 1000.0))
    edge = 10 ** (p['zero_edge_db'] / 20.0)
    ctx = int(round(rate * 0.005))
    out = []
    for s, e in zip(starts, ends):
        if e - s < lo or e - s > hi or s == 0 or e >= n:
            continue
        enter = np.abs(x[s - 1]).max()
        leave = np.abs(x[e]).max()
        if enter < edge or leave < edge:
            continue
        pre = np.sqrt((x[max(0, s - ctx):s] ** 2).mean())
        post = np.sqrt((x[e:e + ctx] ** 2).mean())
        out.append(dict(kind='dropout', sample=int(s), t=s / rate, duration_ms=round((e - s) * 1000.0 / rate, 3),
                        frames=int(e - s), enter_dbfs=round(db(enter), 1), leave_dbfs=round(db(leave), 1),
                        context_dbfs=round(db(max(pre, post)), 1)))
    return out


def block_frames_for(rate, block_32k=160):
    v = Fraction(block_32k * rate, 32000)
    return int(v) if v.denominator == 1 else None


def detect_repeats(x, rate, p, info, block_32k=160, multiples=(1, 2, 3, 4), allow_resample=False):
    """Exact repeats of a 5 ms block (and 2x-4x bursts). Returns (events, note)."""
    L = block_frames_for(rate, block_32k)
    note = None
    y, yrate, exact_mode = x, rate, True
    if L is None:
        if not allow_resample or resample_poly is None:
            return [], (f"repeat detection skipped: a {block_32k}-frame 32 kHz block is not a whole number of "
                        f"samples at {rate} Hz (use --repeat-resample to test a 32 kHz copy)")
        fr = Fraction(32000, rate).limit_denominator(2000)
        y = resample_poly(x, fr.numerator, fr.denominator, axis=0)
        yrate, L, exact_mode = 32000, block_32k, False
        note = f"repeat detection ran on a 32 kHz resampled copy (near-exact match, -40 dB)"
    n = len(y)
    level = 10 ** (p['repeat_level_db'] / 20.0)
    out = []
    seen = np.zeros(n, bool)
    for k in multiples:
        lag = k * L
        if n <= lag + L:
            continue
        a, b = y[lag:], y[:-lag]
        if exact_mode:
            eq = np.all(a == b, axis=1)
        else:
            dsum, _ = centred_sums(((a - b) ** 2).sum(axis=1), L // 2)
            ssum, _ = centred_sums((a ** 2).sum(axis=1), L // 2)
            eq = dsum <= 1e-4 * np.maximum(ssum, 1e-20)
        starts, ends = run_bounds(eq)
        for s, e in zip(starts, ends):
            if e - s < L:
                continue
            i0 = s + lag                     # the repeated copy starts here
            seg = y[i0:e + lag]
            rms = float(np.sqrt((seg ** 2).mean()))
            if rms < level or seen[i0]:
                continue
            if np.abs(np.diff(seg, axis=0)).max() <= 2 * max(info.get('lsb', 0.0), 1e-9):
                continue                     # constant material: a hold, reported elsewhere
            periodic = False
            for q in range(2, lag):
                if lag % q:
                    continue
                pl = lag // q
                if pl < 2:
                    continue
                aa, bb = y[i0:e + lag], y[i0 - pl:e + lag - pl]
                if exact_mode:
                    frac = np.mean(np.all(aa == bb, axis=1))
                else:
                    frac = 1.0 if ((aa - bb) ** 2).sum() <= 1e-4 * (aa ** 2).sum() else 0.0
                if frac >= 0.9:
                    periodic = True
                    break
            if periodic:
                continue
            seen[i0:e + lag] = True
            t0 = i0 / yrate
            out.append(dict(kind='repeat', sample=int(round(t0 * rate)), t=t0, lag_frames=int(lag),
                            lag_ms=round(lag * 1000.0 / yrate, 3), blocks=round((e - s) / L, 2),
                            duration_ms=round((e - s) * 1000.0 / yrate, 3), block_dbfs=round(db(rms), 1),
                            exact=exact_mode))
    return out, note


def analyse_rate_csv(path, rate, x, p):
    """Starved callbacks and resampling-ratio behaviour from <dump>.rate.csv (columns ms, frames,
    rate, buffered, starved). Positions come from the cumulative frame count, which is exact; the
    ms column is GetTickCount64 and moves in 15.6 ms steps, so it is not used for timing."""
    rows = list(csv.DictReader(open(path, newline='')))
    n = len(x)
    pos = 0
    events = []
    cur = None
    ratios, ends = [], []
    for r in rows:
        f = int(r['frames'])
        s = int(r['starved'])
        ratios.append(float(r['rate']))
        ends.append(pos + f)
        if s:
            # A callback runs in microseconds, so the ring state is fixed while it runs: either it
            # ran dry part way (its last frames starved) or it started dry (all of it starved). A
            # fully starved callback right after a starved one continues that gap.
            start = pos + f - s
            if cur is not None and cur['end'] == pos and s == f:
                cur['end'] = pos + f
                cur['frames'] += s
                cur['callbacks'] += 1
            else:
                cur = dict(start=start, end=pos + f, frames=s, callbacks=1)
                events.append(cur)
        pos += f
    sums = Sums((x * x).mean(axis=1))
    w = int(round(rate * 0.020))
    out = []
    for ev in events:
        s, e = ev['start'], ev['end']
        level = max(sums.rms(s - w, s), sums.rms(e, e + w))
        out.append(dict(kind='starvation', sample=int(s), t=s / rate, frames=int(ev['frames']),
                        duration_ms=round(ev['frames'] * 1000.0 / rate, 3), callbacks=ev['callbacks'],
                        context_dbfs=round(db(level), 1), audible=bool(db(level) > -60.0)))
    cents = 1200.0 * np.log2(np.maximum(np.array(ratios), 1e-9)) if ratios else np.zeros(0)
    steps = np.diff(cents) if len(cents) > 1 else np.zeros(0)
    step_events = []
    for i in np.flatnonzero(np.abs(steps) > p['rate_step_cents']):
        s = ends[i]
        step_events.append(dict(kind='rate_step', sample=int(s), t=s / rate, step_cents=round(float(steps[i]), 3),
                                from_cents=round(float(cents[i]), 2), to_cents=round(float(cents[i + 1]), 2)))
    times = np.array(ends) / rate
    summary = dict(rows=len(rows), frames=int(pos), wav_frames=int(n), frames_match=bool(pos == n),
                   starvation_events=len(out), starved_frames=int(sum(e['frames'] for e in out)),
                   starved_ms=round(sum(e['frames'] for e in out) * 1000.0 / rate, 1),
                   audible_starvation_events=sum(1 for e in out if e['audible']))
    if len(cents):
        summary.update(cents_min=round(float(cents.min()), 2), cents_max=round(float(cents.max()), 2),
                       cents_median=round(float(np.median(cents)), 2), cents_std=round(float(cents.std()), 2),
                       cents_abs_p95=round(float(np.percentile(np.abs(cents), 95)), 2),
                       time_beyond_5_cents_pct=round(100.0 * float(np.mean(np.abs(cents) > 5)), 1),
                       time_beyond_10_cents_pct=round(100.0 * float(np.mean(np.abs(cents) > 10)), 1),
                       time_beyond_20_cents_pct=round(100.0 * float(np.mean(np.abs(cents) > 20)), 1),
                       largest_step_cents=round(float(np.abs(steps).max()), 3) if len(steps) else 0.0,
                       steps_over_threshold=len(step_events))
        # hunting: the strongest oscillation of the ratio (detrended), if the control loop swings
        if len(cents) > 64:
            dt = float(np.median(np.diff(times))) if len(times) > 1 else 0.01
            v = cents - np.convolve(cents, np.ones(31) / 31, mode='same')
            spec = np.abs(np.fft.rfft(v * np.hanning(len(v))))
            fr = np.fft.rfftfreq(len(v), dt)
            k = int(np.argmax(spec[1:]) + 1)
            summary.update(oscillation_hz=round(float(fr[k]), 3),
                           oscillation_cents_rms=round(float(v.std()), 3))
    return out, step_events, summary, (times, cents)


def attach(events, p, rate):
    """Clicks and cutoffs within attach_ms of a hold/dropout/repeat edge belong to that event."""
    anchors = [e for e in events if e['kind'] in ('hold', 'dropout', 'repeat')]
    if not anchors:
        return
    tol = p['attach_ms'] / 1000.0
    edges = []
    for i, a in enumerate(anchors):
        a['id'] = a.get('id', i)
        edges.append((a['t'], a))
        edges.append((a['t'] + a.get('duration_ms', 0) / 1000.0 + a.get('zero_tail_ms', 0) / 1000.0, a))
    ts = np.array([t for t, _ in edges])
    for e in events:
        if e['kind'] not in ('click', 'cutoff', 'onset'):
            continue
        j = int(np.argmin(np.abs(ts - e['t'])))
        if abs(ts[j] - e['t']) <= tol:
            e['part_of'] = f"{edges[j][1]['kind']}@{edges[j][1]['t']:.4f}"


# ---------------------------------------------------------------------------------------------
# Driver

def analyse(x, rate, info, p=None, rate_csv=None, repeat_resample=False, block_32k=160):
    p = dict(DEFAULTS, **(p or {}))
    events, sweep = detect_clicks(x, rate, p, info)
    events += detect_holds(x, rate, p, info)
    events += detect_zero_runs(x, rate, p, info)
    reps, rep_note = detect_repeats(x, rate, p, info, block_32k=block_32k, allow_resample=repeat_resample)
    events += reps
    rate_summary = None
    curve = None
    if rate_csv:
        starve, steps, rate_summary, curve = analyse_rate_csv(rate_csv, rate, x, p)
        events += starve + steps
    attach(events, p, rate)
    events.sort(key=lambda e: e['t'])
    dur = len(x) / rate
    kinds = ('click', 'cutoff', 'onset', 'hold', 'dropout', 'repeat', 'starvation', 'rate_step')
    counts = {k: 0 for k in kinds}
    for e in events:
        if e['kind'] in ('click', 'cutoff', 'onset') and 'part_of' in e:
            continue
        counts[e['kind']] += 1
    defects = counts['click'] + counts['cutoff'] + counts['hold'] + counts['dropout'] + counts['repeat']
    peak = float(np.abs(x).max()) if len(x) else 0.0
    full = 1.0 - (info.get('lsb') or 0.0)
    report = dict(tool='audio_clicks', version=VERSION, file=info.get('path'), format=info.get('format'),
                  rate=rate, channels=int(x.shape[1]), duration_s=round(dur, 3), params=p,
                  levels=dict(rms_dbfs=round(db(np.sqrt((x ** 2).mean())) if len(x) else -240, 2),
                              peak_dbfs=round(db(peak), 2),
                              clipped_samples=int((np.abs(x) >= full).sum()) if info.get('integer') else int((np.abs(x) >= 1.0).sum()),
                              silent_fraction=round(float(np.mean(np.all(x == 0, axis=1))), 4) if len(x) else 1.0),
                  counts=counts, defects=defects,
                  defects_per_minute=round(defects * 60.0 / dur, 2) if dur > 0 else 0.0,
                  click_sweep={str(k): v for k, v in sweep.items()},
                  notes=[n for n in (info.get('header_note'), rep_note) if n])
    if rate_summary is not None:
        report['rate_csv'] = rate_summary
    report['events'] = events
    return report, curve


def summary_text(rep, max_list=25):
    c = rep['counts']
    lines = [f"{rep['file']}: {rep['duration_s']:.1f} s, {rep['rate']} Hz, {rep['channels']} ch, {rep['format']}; "
             f"RMS {rep['levels']['rms_dbfs']} dBFS, peak {rep['levels']['peak_dbfs']} dBFS, "
             f"clipped samples {rep['levels']['clipped_samples']}",
             f"defects {rep['defects']} ({rep['defects_per_minute']} per minute): clicks {c['click']}, cutoffs {c['cutoff']}, "
             f"holds {c['hold']}, dropouts {c['dropout']}, repeats {c['repeat']}; hard onsets (not counted) {c['onset']}",
             "clicks+cutoffs by peak z (same events, looser to stricter): " +
             ", ".join(f"z>={k}: {v}" for k, v in rep['click_sweep'].items())]
    if 'rate_csv' in rep:
        r = rep['rate_csv']
        lines.append(f"rate CSV: {r['starvation_events']} starvation events ({r['audible_starvation_events']} in audible audio), "
                     f"{r['starved_ms']} ms starved; ratio {r.get('cents_min')}..{r.get('cents_max')} cents "
                     f"(median {r.get('cents_median')}, |p95| {r.get('cents_abs_p95')}, beyond 10 cents "
                     f"{r.get('time_beyond_10_cents_pct')}% of callbacks), largest step {r.get('largest_step_cents')} cents"
                     + ("" if r['frames_match'] else f"; WARNING CSV frames {r['frames']} != WAV frames {r['wav_frames']}"))
    for n in rep.get('notes', []):
        lines.append("note: " + n)
    shown = [e for e in rep['events'] if e['kind'] not in ('onset', 'rate_step') and 'part_of' not in e]
    shown.sort(key=lambda e: -severity(e))
    if shown:
        lines.append(f"top {min(max_list, len(shown))} of {len(shown)} events by severity:")
        for e in shown[:max_list]:
            lines.append("  " + describe(e))
    return "\n".join(lines)


def severity(e):
    k = e['kind']
    if k in ('click', 'cutoff'):
        return e['z']
    if k in ('hold', 'dropout'):
        return 20 + e['duration_ms'] + max(0.0, 60 + e.get('context_dbfs', -60))
    if k == 'repeat':
        return 30 + e['blocks']
    if k == 'starvation':
        return (10 + e['duration_ms']) if e['audible'] else 0
    return 0


def describe(e):
    k = e['kind']
    head = f"{e['t']:9.4f} s  {k:<10}"
    if k in ('click', 'cutoff', 'onset'):
        return (f"{head} z {e['z']:6.1f}  size {e['size_dbfs']:6.1f} dBFS  local {e['local_dbfs']:6.1f} dBFS  "
                f"energy change {e['energy_change_db']:+.1f} dB  ch {e['channel']}")
    if k == 'hold':
        return (f"{head} {e['duration_ms']:.3f} ms {'exact' if e['exact'] else 'decaying x' + str(e['ratio'])}"
                f" at {e['held_dbfs']} dBFS, context {e['context_dbfs']} dBFS"
                + (f", then {e['zero_tail_ms']} ms of zeros" if e['zero_tail_ms'] else "")
                + ("" if e['resumed'] else ", did not resume within 50 ms"))
    if k == 'dropout':
        return f"{head} {e['duration_ms']:.3f} ms of zeros, entered at {e['enter_dbfs']} dBFS, left at {e['leave_dbfs']} dBFS"
    if k == 'repeat':
        return f"{head} {e['blocks']} x {e['lag_ms']} ms block repeated at {e['block_dbfs']} dBFS"
    if k == 'starvation':
        return f"{head} {e['duration_ms']:.3f} ms starved over {e['callbacks']} callback(s), audio around it {e['context_dbfs']} dBFS"
    if k == 'rate_step':
        return f"{head} ratio step {e['step_cents']:+.3f} cents ({e['from_cents']} -> {e['to_cents']})"
    return head


def plot(rep, x, rate, curve, path, top=12):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        return "matplotlib not installed; no plot written"
    colors = dict(click='#d62728', cutoff='#9467bd', onset='#bbbbbb', hold='#1f77b4', dropout='#17becf',
                  repeat='#ff7f0e', starvation='#2ca02c', rate_step='#8c564b')
    rows = 3 if curve is not None else 2
    fig, axes = plt.subplots(rows, 1, figsize=(14, 3.2 * rows), sharex=True)
    hop = max(1, int(rate * 0.010))
    m = (x ** 2).mean(axis=1)
    k = len(m) // hop
    env = 10 * np.log10(m[:k * hop].reshape(k, hop).mean(axis=1) + 1e-14)
    tt = np.arange(k) * hop / rate
    axes[0].plot(tt, env, lw=0.6, color='#444444')
    axes[0].set_ylabel('dBFS (10 ms RMS)')
    axes[0].set_ylim(-100, 3)
    for e in rep['events']:
        if e['kind'] == 'onset' or 'part_of' in e:
            continue
        axes[0].axvline(e['t'], color=colors.get(e['kind'], 'k'), lw=0.7, alpha=0.8)
    axes[0].set_title(Path(rep['file']).name + f": {rep['defects']} defects, " +
                      ", ".join(f"{k} {v}" for k, v in rep['counts'].items() if v))
    zs = [(e['t'], e['z'], e['kind']) for e in rep['events'] if e['kind'] in ('click', 'cutoff', 'onset')]
    if zs:
        axes[1].scatter([a for a, _, _ in zs], [b for _, b, _ in zs], s=10,
                        c=[colors[c] for _, _, c in zs])
    axes[1].axhline(rep['params']['threshold'], color='k', lw=0.6, ls='--')
    axes[1].set_yscale('log')
    axes[1].set_ylabel('click z (residual / local RMS)')
    if curve is not None:
        axes[2].plot(curve[0], curve[1], lw=0.7, color='#2ca02c')
        for e in rep['events']:
            if e['kind'] == 'starvation':
                axes[2].axvline(e['t'], color='#d62728' if e['audible'] else '#cccccc', lw=0.6)
        axes[2].set_ylabel('resampling ratio (cents)')
    axes[-1].set_xlabel('seconds')
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)
    shown = [e for e in rep['events'] if e['kind'] not in ('onset', 'rate_step') and 'part_of' not in e]
    shown.sort(key=lambda e: -severity(e))
    shown = shown[:top]
    if shown:
        zpath = str(Path(path).with_name(Path(path).stem + '-zoom.png'))
        cols = 3
        nrows = (len(shown) + cols - 1) // cols
        fig, axes = plt.subplots(nrows, cols, figsize=(15, 2.6 * nrows), squeeze=False)
        half = int(rate * 0.004)
        for ax, e in zip(axes.ravel(), shown):
            s = e['sample']
            a, b = max(0, s - half), min(len(x), s + half + int(rate * e.get('duration_ms', 0) / 1000.0))
            t = (np.arange(a, b) - s) * 1000.0 / rate
            for c in range(min(2, x.shape[1])):
                ax.plot(t, x[a:b, c], lw=0.8)
            ax.axvline(0, color=colors.get(e['kind'], 'k'), lw=0.8)
            ax.set_title(f"{e['t']:.4f} s {e['kind']}", fontsize=9)
            ax.tick_params(labelsize=7)
        for ax in axes.ravel()[len(shown):]:
            ax.axis('off')
        fig.tight_layout()
        fig.savefig(zpath, dpi=100)
        plt.close(fig)
    return None


# ---------------------------------------------------------------------------------------------
# Self-test and injection test

def _synth(rate, seconds=16.0, seed=7):
    """A mix with sustained harmonic notes, hard percussive hits, fast-attack sine blips, a hard
    onset from silence, near-silence with 1-LSB blips, and a stretch of digital silence."""
    rng = np.random.default_rng(seed)
    n = int(rate * seconds)
    t = np.arange(n) / rate
    x = np.zeros((n, 2))
    busy = int(rate * min(11.5, seconds - 0.5))
    # notes: 300 ms each, 5 ms raised-cosine attack/release, 3 harmonics, slow vibrato
    note = int(rate * 0.3)
    ramp = int(rate * 0.005)
    env = np.ones(note)
    env[:ramp] = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, ramp))
    env[-ramp:] = env[:ramp][::-1]
    for i, s in enumerate(range(0, busy - note, note)):
        f0 = 110.0 * 2 ** (rng.integers(0, 24) / 12.0)
        tt = t[s:s + note] - t[s]
        vib = 1 + 0.003 * np.sin(2 * np.pi * 5 * tt)
        ph = 2 * np.pi * f0 * np.cumsum(vib) / rate
        v = (np.sin(ph) + 0.5 * np.sin(2 * ph + 0.3) + 0.25 * np.sin(3 * ph + 1.1)) * env * 0.06
        pan = 0.3 + 0.4 * rng.random()
        x[s:s + note, 0] += v * pan
        x[s:s + note, 1] += v * (1 - pan)
    # percussion: noise bursts with a 1.5 ms attack and 80 ms decay, every 400 ms
    taper = int(rate * 0.01)
    fade_out = 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, taper))
    for s in range(int(rate * 0.2), busy - int(rate * 0.25), int(rate * 0.4)):
        m = int(rate * 0.25)
        a = int(rate * 0.0015)
        e = np.exp(-np.arange(m) / (rate * 0.08))
        e[:a] *= 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, a))
        e[-taper:] *= fade_out
        x[s:s + m] += rng.standard_normal((m, 2)) * e[:, None] * 0.08
    # blips: 2 kHz with a 1 ms attack, 40 ms decay, every 700 ms
    for s in range(int(rate * 0.45), busy - int(rate * 0.12), int(rate * 0.7)):
        m = int(rate * 0.12)
        a = int(rate * 0.001)
        e = np.exp(-np.arange(m) / (rate * 0.04))
        e[:a] *= 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, a))
        e[-taper:] *= fade_out
        v = np.sin(2 * np.pi * 2000 * np.arange(m) / rate) * e * 0.1
        x[s:s + m, 0] += v
        x[s:s + m, 1] += 0.7 * v
    if seconds >= 15.0:
        # silence 12.0-14.0 s, with a hard onset (a sine starting at its peak) at 13.0 s
        x[int(rate * 12.0):int(rate * 14.0)] = 0.0
        s = int(rate * 13.0)
        m = int(rate * 0.2)
        e = np.exp(-np.arange(m) / (rate * 0.05))
        e[-taper:] *= fade_out
        x[s:s + m, 0] = 0.1 * np.cos(2 * np.pi * 440 * np.arange(m) / rate) * e
        x[s:s + m, 1] = x[s:s + m, 0]
        # 1-LSB blips in near-silence at 14.5 s (inaudible: must not count)
        x[int(rate * 14.5):int(rate * 14.5) + 3, :] = 1 / 32768.0
    return np.round(x * 32767) / 32768.0          # 16-bit values, like the dumps


def _inject_all(x, rate, positions):
    """Applies each defect at its position; returns (new signal, [(kind expected, time)])."""
    y = x.copy()
    truth = []
    L = int(round(160 * rate / 32000))
    f = 0.98 ** (32000.0 / rate)
    q = lambda v: np.round(v * 32767) / 32768.0
    for kind, t in positions:
        s = int(rate * t)
        if kind == 'step':          # a jump that then drifts back (a DC-blocked step)
            m = min(len(y) - s, int(rate * 0.4))
            y[s:s + m] += 0.05 * np.exp(-np.arange(m) / (rate * 0.05))[:, None]
            truth.append(('click', t))
        elif kind == 'spike':
            y[s, 0] += 0.04
            truth.append(('click', t))
        elif kind == 'splice':      # a dropped 5 ms block, at the spot with the largest jump nearby
            cands = np.arange(s - 200, s + 200)
            k = cands[np.argmax(np.abs(y[cands + L] - y[cands]).max(axis=1))]
            y = np.concatenate([y[:k], y[k + L:], np.zeros((L, y.shape[1]))])
            truth.append(('click', k / rate))
        elif kind in ('fade', 'hold'):  # starvation: freeze (decaying for 'fade'), then resume late
            # while the waveform is loud and moving (a freeze at a peak or a zero crossing leaves
            # nothing to see; inject_test measures those odds on real audio)
            cands = np.arange(s - int(rate * 0.002), s + int(rate * 0.002))
            loud = np.abs(y[cands - 1]).max(axis=1) >= 0.02
            slope = np.abs(y[cands - 1] - y[cands - 2]).max(axis=1) * loud
            s = int(cands[np.argmax(slope)])
            t = s / rate
            m = 40 if kind == 'fade' else 64
            last = y[s - 1].copy()
            held = np.zeros((m, y.shape[1]))
            for i in range(m):
                last = q(last * f) if kind == 'fade' else last
                held[i] = last
            tail = y[s:].copy()
            ramp = int(rate * 0.005)
            off = held[-1] - tail[0]
            tail[:ramp] += off[None, :] * np.linspace(1, 0, ramp)[:, None]
            y = np.concatenate([y[:s], held, tail])[:len(x)]
            truth.append(('hold', t))
        elif kind == 'zeros':
            m = int(rate * 0.005)
            y = np.concatenate([y[:s], np.zeros((m, y.shape[1])), y[s:]])[:len(x)]
            truth.append(('dropout', t))
        elif kind == 'repeat':
            blk = y[s - L:s].copy()
            y = np.concatenate([y[:s], blk, y[s:]])[:len(x)]
            truth.append(('repeat', t))
    return np.clip(y, -1, 1 - 1 / 32768.0), truth


def selftest():
    ok = True
    lines = []
    for rate in (32000, 44100, 48000):
        x = _synth(rate)
        info = dict(path=f'synthetic@{rate}', format='int16', integer=True, lsb=1 / 32768.0)
        rep, _ = analyse(x, rate, info)
        bad = [e for e in rep['events'] if e['kind'] in ('click', 'cutoff', 'hold', 'dropout', 'repeat')]
        onsets = [e for e in rep['events'] if e['kind'] == 'onset']
        hard = any(abs(e['t'] - 13.0) < 0.003 for e in onsets)
        lines.append(f"{rate} Hz clean: {len(bad)} false defects (want 0), hard onset at 13.0 s classified as onset: {hard}")
        if bad:
            ok = False
            for e in bad[:5]:
                lines.append("    false: " + describe(e))
        # between the noise bursts (a -26 dBFS step inside a -25 dBFS noise burst is masked, and
        # the detector rightly misses it) and away from the blips; tonal material everywhere
        plan = [('step', 1.32), ('spike', 2.12), ('splice', 2.92), ('fade', 3.72), ('hold', 4.52),
                ('zeros', 5.72), ('repeat', 6.52), ('step', 7.32), ('spike', 8.52), ('fade', 9.32),
                ('hold', 10.12)]
        if block_frames_for(rate) is None:
            plan = [p for p in plan if p[0] != 'repeat']
        y, truth = _inject_all(x, rate, plan)
        rep, _ = analyse(y, rate, info)
        found = 0
        for want, t in truth:
            hit = [e for e in rep['events'] if abs(e['t'] - t) <= 0.002 and
                   (e['kind'] == want or (want == 'click' and e['kind'] == 'cutoff'))]
            found += bool(hit)
            if not hit:
                ok = False
                near = [describe(e) for e in rep['events'] if abs(e['t'] - t) <= 0.01]
                lines.append(f"    MISSED {want} at {t:.4f} s; nearby: {near}")
        extra = [e for e in rep['events'] if e['kind'] in ('click', 'cutoff', 'hold', 'dropout', 'repeat')
                 and 'part_of' not in e and not any(abs(e['t'] - t) <= 0.012 for _, t in truth)]
        lines.append(f"{rate} Hz injected: found {found} of {len(truth)} defects, {len(extra)} unrelated events (want 0)")
        if extra:
            ok = False
            for e in extra[:5]:
                lines.append("    extra: " + describe(e))
    # rate CSV: a starved callback and a ratio step, positions exact
    import tempfile
    rate = 44100
    x = _synth(rate, seconds=4.0)
    with tempfile.TemporaryDirectory() as d:
        pth = Path(d) / 'r.csv'
        rows = ['ms,frames,rate,buffered,starved']
        pos = 0
        for i in range(len(x) // 441):
            ratio = 1.0 if i < 200 else 1.003
            starved = 37 if i == 150 else 0
            rows.append(f"{i * 10},441,{ratio:.6f},600,{starved}")
            pos += 441
        pth.write_text("\n".join(rows) + "\n")
        info = dict(path='synthetic-csv', format='int16', integer=True, lsb=1 / 32768.0)
        rep, _ = analyse(x[:pos], rate, info, rate_csv=str(pth))
    st = [e for e in rep['events'] if e['kind'] == 'starvation']
    rs = [e for e in rep['events'] if e['kind'] == 'rate_step']
    csv_ok = (len(st) == 1 and st[0]['sample'] == 151 * 441 - 37 and st[0]['frames'] == 37 and
              len(rs) == 1 and rs[0]['sample'] == 200 * 441)
    lines.append(f"rate CSV: starvation at sample {st[0]['sample'] if st else None} (want {151 * 441 - 37}), "
                 f"ratio step at {rs[0]['sample'] if rs else None} (want {200 * 441}): {'ok' if csv_ok else 'WRONG'}")
    ok &= csv_ok
    print("\n".join(lines))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def inject_test(path, raw=None, trials=40, seed=3, p=None):
    """Sensitivity on real audio: steps of several sizes, 5 ms splices and 0.5 ms holds are injected
    one at a time at random loud spots of the capture; reports how many the detector finds."""
    rate, x, info = read_audio(path, raw)
    x = x[:, :2]
    rng = np.random.default_rng(seed)
    base, _ = analyse(x, rate, info, p)
    base_t = np.array([e['t'] for e in base['events'] if e['kind'] in ('click', 'cutoff', 'hold', 'dropout', 'repeat')])
    hop = int(rate * 0.05)
    m = (x ** 2).mean(axis=1)
    k = len(m) // hop
    lev = 10 * np.log10(m[:k * hop].reshape(k, hop).mean(axis=1) + 1e-14)
    loud = np.flatnonzero(lev > -40)
    loud = loud[(loud > 2) & (loud < k - 3)]
    if not len(loud):
        raise SystemExit("no stretch above -40 dBFS to inject into")
    print(f"{path}: baseline {len(base_t)} defects; injecting {trials} of each kind at spots above -40 dBFS")
    res = {}
    kinds = [('step -50 dBFS', 'step', 10 ** (-50 / 20)), ('step -40 dBFS', 'step', 0.01), ('step -34 dBFS', 'step', 10 ** (-34 / 20)),
             ('step -26 dBFS', 'step', 0.05), ('step -20 dBFS', 'step', 0.1), ('splice 5 ms', 'splice', 0),
             ('hold 0.5 ms (decaying)', 'fade', 0), ('hold 1.5 ms (exact)', 'hold', 0)]
    f = 0.98 ** (32000.0 / rate)
    for label, kind, size in kinds:
        hits = 0
        for _ in range(trials):
            c = int(rng.choice(loud)) * hop + int(rng.integers(0, hop))
            a, b = max(0, c - int(rate * 0.3)), min(len(x), c + int(rate * 0.3))
            seg = x[a:b].copy()
            s = c - a
            if kind == 'step':
                seg[s:] += size * (1 if rng.random() < 0.5 else -1)
            elif kind == 'splice':
                L = int(round(160 * rate / 32000))
                seg = np.concatenate([seg[:s], seg[s + L:], np.zeros((L, seg.shape[1]))])
            else:
                mlen = int(rate * (0.0005 if kind == 'fade' else 0.0015))
                last = seg[s - 1].copy()
                held = []
                for i in range(mlen):
                    if kind == 'fade':
                        last = np.trunc(last * f * 32768) / 32768
                    held.append(last.copy())
                tail = seg[s:].copy()
                ramp = int(rate * 0.005)
                tail[:ramp] += (held[-1] - tail[0])[None, :] * np.linspace(1, 0, ramp)[:, None]
                seg = np.concatenate([seg[:s], np.array(held), tail])[:b - a]
            if info.get('integer'):
                seg = np.round(np.clip(seg, -1, 1) * 32768) / 32768
            r, _ = analyse(seg, rate, info, p)
            t0 = s / rate
            got = [e for e in r['events'] if e['kind'] in ('click', 'cutoff', 'hold', 'dropout', 'repeat')
                   and abs(e['t'] - t0) <= 0.002]
            hits += bool(got)
        res[label] = hits
        print(f"  {label:<24} found {hits:3d} of {trials}")
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('file', nargs='?')
    ap.add_argument('--raw', help='headerless PCM as format:rate:channels (f32le, s16le, f64le, s32le)')
    ap.add_argument('--rate-csv', help='the <dump>.rate.csv of a MELEE_AUDIO_DEVICE_DUMP capture')
    ap.add_argument('--json', help='write the full report here')
    ap.add_argument('--png', help='write an overview plot here (and <name>-zoom.png)')
    ap.add_argument('--start', type=float, default=0.0, help='seconds')
    ap.add_argument('--end', type=float, default=None, help='seconds')
    ap.add_argument('--channels', default='0,1', help='channel indices to analyse (default 0,1)')
    ap.add_argument('--threshold', type=float, default=DEFAULTS['threshold'], help='click z threshold (default 12)')
    ap.add_argument('--min-level-db', type=float, default=DEFAULTS['min_level_db'],
                    help='smallest discontinuity counted, dBFS (default -66)')
    ap.add_argument('--block-32k', type=int, default=160, help='repeat block in 32 kHz frames (default 160)')
    ap.add_argument('--repeat-resample', action='store_true', help='test repeats on a 32 kHz copy when the rate needs it')
    ap.add_argument('--max-list', type=int, default=25)
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--inject-test', metavar='WAV', help='measure detection of injected defects in a real capture')
    ap.add_argument('--trials', type=int, default=40)
    a = ap.parse_args()
    if sosfiltfilt is None:
        raise SystemExit("scipy is required: py -3.12 -m pip install --user numpy scipy")
    if a.selftest:
        return selftest()
    if a.inject_test:
        inject_test(a.inject_test, a.raw, a.trials)
        return 0
    if not a.file:
        ap.error('a file is required')
    rate, x, info = read_audio(a.file, a.raw)
    chans = [int(c) for c in a.channels.split(',') if c.strip() != '' and int(c) < x.shape[1]]
    x = x[:, chans]
    s0 = int(a.start * rate)
    s1 = len(x) if a.end is None else min(len(x), int(a.end * rate))
    offset = s0
    x = x[s0:s1]
    p = dict(threshold=a.threshold, min_level_db=a.min_level_db)
    csv_path = a.rate_csv
    if csv_path and offset:
        raise SystemExit("--rate-csv needs the whole file (no --start)")
    rep, curve = analyse(x, rate, info, p, rate_csv=csv_path, repeat_resample=a.repeat_resample, block_32k=a.block_32k)
    if offset:
        for e in rep['events']:
            e['t'] += offset / rate
            e['sample'] += offset
        rep['analysed_from_s'] = a.start
    print(summary_text(rep, a.max_list))
    if a.json:
        Path(a.json).parent.mkdir(parents=True, exist_ok=True)
        Path(a.json).write_text(json.dumps(rep, indent=1) + "\n")
    if a.png:
        Path(a.png).parent.mkdir(parents=True, exist_ok=True)
        msg = plot(rep, x, rate, curve, a.png)
        if msg:
            print(msg)
    return 0


if __name__ == '__main__':
    sys.exit(main())
