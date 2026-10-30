"""Rebuild trusted pro arrays from an existing Lunar bulk job, one archive at a time."""
import argparse
from concurrent.futures import ProcessPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import time
import urllib.request
import zipfile

import numpy as np
import extract
from players import PLAYERS
from collect_verified import split_of
import slpz

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))
API = 'https://api.lunarmelee.com/api'
UA = 'MeleeUnlocked-practice-AI-builder (verified bulk offline demonstrations)'


def call(path, body=None):
    headers = {'User-Agent': UA, 'X-Client-Id': (HERE / 'client-id.txt').read_text().strip(),
               'Content-Type': 'application/json'}
    req = urllib.request.Request(API + path, data=json.dumps(body).encode() if body else None, headers=headers)
    with urllib.request.urlopen(req, timeout=90) as response:
        return json.load(response)


def parse(item):
    pro, rid, packed = item
    try:
        raw = packed if packed[:3] == b'{U\x03' else slpz.decompress(packed)
        with tempfile.TemporaryDirectory(prefix='mu-verified-') as temp:
            path = Path(temp) / (rid + '.slp')
            path.write_bytes(raw)
            data = extract.extract(path, pro, 0)
        if data is None:
            return rid, None, 'identity_character_or_frames'
        date = str(data['source_start_at'])[:10]
        if len(date) != 10:
            return rid, None, 'missing_date'
        group = 'day:' + date
        data['split'] = np.array(split_of(group))
        data['split_group'] = np.array(group)
        return rid, data, None
    except BaseException as exc:
        # peppi raises a pyo3 PanicException for a few structurally broken SLPs.
        # That exception type cannot be pickled across ProcessPoolExecutor on
        # Windows, so convert even panics to plain text inside the worker.
        try:
            detail = str(exc)[:150]
        except BaseException:
            detail = 'unprintable parser exception'
        return rid, None, type(exc).__name__ + ': ' + detail


def equivalent_legacy(pro, rid, data):
    """Retire only reproducible legacy arrays proven equal after final-frame selection."""
    root = (HERE / 'data' / pro / 'frames').resolve()
    legacy = (root / (rid + '.npz')).resolve()
    if legacy.parent != root or not legacy.is_file():
        return None
    with np.load(legacy, allow_pickle=False) as old:
        indices = extract.canonical_indices(old['frame'])
        length = len(old['frame'])
        for key in old.files:
            if key not in data:
                return None
            value = old[key]
            if value.ndim and len(value) == length:
                value = value[indices]
            if not np.array_equal(value, data[key], equal_nan=True):
                return None
    return legacy, hashlib.sha256(legacy.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pro', choices=list(PLAYERS), required=True)
    ap.add_argument('--job', required=True)
    ap.add_argument('--workers', type=int, default=4)
    ap.add_argument('--archive-home', type=Path, default=None,
                    help='temporary ZIP directory; defaults to MELEE_GHOST_ARCHIVE_HOME or MELEE_GHOST_AI_HOME')
    args = ap.parse_args()
    folder = HERE / 'data' / args.pro / 'verified'
    frames = folder / 'frames'
    frames.mkdir(parents=True, exist_ok=True)
    manifest = folder / 'manifest.jsonl'
    current = {r['replay_id']: r for r in (json.loads(line) for line in manifest.read_text().splitlines())} if manifest.exists() else {}
    done = {rid for rid, row in current.items() if row['status'] == 'accepted'}
    hashes = {r['source_sha256'] for r in current.values() if r['status'] == 'accepted'}
    status = call('/jobs/' + args.job)
    if not status.get('downloadReady'):
        raise SystemExit('Bulk job is not ready: ' + str(status.get('status')))
    archive_home = (args.archive_home or Path(os.environ.get('MELEE_GHOST_ARCHIVE_HOME', HERE))).resolve()
    archive_home.mkdir(parents=True, exist_ok=True)
    archive = archive_home / ('verified-bulk-' + args.pro + '.zip')
    size = status['bundleSize']
    if not archive.exists() or archive.stat().st_size != size:
        free_data = shutil.disk_usage(HERE).free
        free_archive = shutil.disk_usage(archive_home).free
        same_volume = archive.drive.casefold() == (HERE / 'data').drive.casefold()
        if same_volume and free_archive < size + 1024**3:
            raise SystemExit(f'Need archive plus 1 GiB reserve on the data volume: {size} bytes, {free_archive} free')
        if not same_volume and (free_archive < size + 256 * 1024**2 or free_data < 1024**3):
            raise SystemExit(f'Need archive plus 256 MiB on its volume and 1 GiB for extracted data: '
                             f'archive={free_archive}, data={free_data}, size={size}')
        link = call('/jobs/' + args.job + '/download')['url']
        print(f'{args.pro}: downloading {size:,} bytes', flush=True)
        req = urllib.request.Request(link, headers={'User-Agent': UA})
        part = archive.with_suffix('.part')
        with urllib.request.urlopen(req, timeout=600) as response, part.open('wb') as output:
            copied, last = 0, time.monotonic()
            while chunk := response.read(4 << 20):
                output.write(chunk); copied += len(chunk)
                if time.monotonic() - last >= 10:
                    print(f'{args.pro}: downloaded {copied:,}/{size:,}', flush=True); last = time.monotonic()
        if part.stat().st_size != size:
            raise RuntimeError('Incomplete bulk archive')
        part.replace(archive)
    accepted, rejected, duplicates = len(done), 0, 0
    start = time.monotonic()
    with zipfile.ZipFile(archive) as z, ProcessPoolExecutor(max_workers=args.workers) as pool:
        mapping = {}
        if 'lunar-manifest.json' in z.namelist():
            m = json.loads(z.read('lunar-manifest.json'))
            for it in (m if isinstance(m, list) else m.get('files') or m.get('replays') or []):
                mapping[it.get('file') or it.get('path') or it.get('name')] = it.get('replayId') or it.get('id')
        names = [name for name in z.namelist() if name.endswith(('.slp', '.slpz'))]
        # Explicit bounded batches keep compressed replays and worker results out of a large queue.
        for offset in range(0, len(names), args.workers * 2):
            jobs = []
            for name in names[offset:offset + args.workers * 2]:
                rid = mapping.get(name) or Path(name).stem
                if rid not in done:
                    jobs.append((args.pro, rid, z.read(name)))
            for rid, data, error in pool.map(parse, jobs):
                retire = None
                row = {'replay_id': rid, 'pro': args.pro, 'bulk_job': args.job, 'extract_version': 2}
                if data is None:
                    row.update(status='rejected', reason=error); rejected += 1
                elif str(data['source_sha256']) in hashes:
                    row.update(status='duplicate', source_sha256=str(data['source_sha256'])); duplicates += 1
                else:
                    dest = frames / (rid + '.npz'); tmp = frames / (rid + '.part.npz')
                    np.savez_compressed(tmp, **data)
                    row.update(status='accepted', source_sha256=str(data['source_sha256']),
                               array_sha256=hashlib.sha256(tmp.read_bytes()).hexdigest(),
                               identity_port=int(data['identity_port']), expected_code=PLAYERS[args.pro]['code'],
                               frames=len(data['frame']), group=str(data['split_group']), split=str(data['split']))
                    tmp.replace(dest)
                    retire = equivalent_legacy(args.pro, rid, data)
                    if retire:
                        row['retired_equivalent_legacy_sha256'] = retire[1]
                    hashes.add(row['source_sha256']); accepted += 1; done.add(rid)
                with manifest.open('a', encoding='utf-8') as output:
                    output.write(json.dumps(row) + '\n')
                if retire:
                    retire[0].unlink()
            if offset % 200 < args.workers * 2:
                print(f'{args.pro}: {min(offset+len(jobs),len(names))}/{len(names)} scanned, {accepted} accepted, '
                      f'{rejected} rejected, {duplicates} duplicates, {time.monotonic()-start:.0f}s', flush=True)
    summary = {'pro': args.pro, 'job': args.job, 'accepted': accepted, 'rejected_this_run': rejected,
               'duplicates_this_run': duplicates, 'seconds': time.monotonic() - start}
    (folder / 'bulk-summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary), flush=True)
    # The downloaded archive is a reproducible cache; remove it only after the whole pass succeeds.
    archive.unlink()


if __name__ == '__main__':
    main()
