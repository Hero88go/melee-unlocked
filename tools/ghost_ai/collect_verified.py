"""Collect attributable pro demonstrations with metadata, hashes and session-grouped splits.

    py -3.12 collect_verified.py --pro amsa --games 40000 --max-downloads 100

Bounded and resumable. Existing legacy arrays are never silently admitted as verified data.
"""
import argparse
import hashlib
import json
import os
import tempfile
import time
import urllib.parse
import urllib.request
from pathlib import Path

import numpy as np
import extract
from players import PLAYERS, normalize_code

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))
API = 'https://api.lunarmelee.com/api'
UA = 'MeleeUnlocked-practice-AI-builder (verified offline demonstrations)'


def get(path, binary=False):
    req = urllib.request.Request(API + path, headers={'User-Agent': UA, 'Accept-Encoding': 'identity'})
    with urllib.request.urlopen(req, timeout=90) as response:
        raw = response.read()
    return raw if binary else json.loads(raw)


def eligible(metadata, pro):
    who = PLAYERS[pro]
    players = metadata.get('players', [])
    matches = [p for p in players if normalize_code(p.get('connectCode')) == who['code']]
    return (len(players) == 2 and len(matches) == 1 and
            matches[0].get('characterId') == who['css_character'] and
            metadata.get('usable') is True and not metadata.get('hasCpu') and
            not any(p.get('cpu') for p in players) and metadata.get('duration', 0) >= 3600)


def split_group(metadata):
    # Conservative day groups keep adjacent sessions and sets out of different splits.
    # Include the tournament identity if supplied; all its games share a split across dates.
    if metadata.get('tournamentName'):
        return 'tournament:' + metadata['tournamentName']
    date = metadata.get('startAt', '')[:10]
    if len(date) != 10:
        return None
    return 'day:' + date


def split_of(group):
    bucket = int(hashlib.sha256(('pro-fidelity-v1:' + group).encode()).hexdigest()[:8], 16) % 100
    return 'test' if bucket < 10 else 'development' if bucket < 20 else 'train'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pro', choices=list(PLAYERS), required=True)
    parser.add_argument('--games', type=int, default=40000)
    parser.add_argument('--max-downloads', type=int, default=100)
    args = parser.parse_args()
    if args.games <= 0 or args.max_downloads <= 0:
        parser.error('counts must be positive')
    who = PLAYERS[args.pro]
    folder = HERE / 'data' / args.pro / 'verified'
    frames = folder / 'frames'
    frames.mkdir(parents=True, exist_ok=True)
    manifest = folder / 'manifest.jsonl'
    old = [json.loads(line) for line in manifest.read_text().splitlines()] if manifest.exists() else []
    # A newer extractor may repair a previously rejected replay. Preserve old audit rows but
    # allow a fresh attempt; only accepted content is part of the deduplication set.
    current = {row['replay_id']: row for row in old}
    done = {rid for rid, row in current.items() if row['status'] in ('accepted', 'duplicate')}
    hashes = {row['source_sha256'] for row in current.values()
              if row['status'] == 'accepted' and row.get('source_sha256')}
    accepted = sum(row['status'] == 'accepted' for row in current.values())
    attempts, page = 0, 1
    while accepted < args.games and attempts < args.max_downloads:
        query = urllib.parse.urlencode({'p1ConnectCode': who['code'], 'p1CharacterId': who['css_character'],
                                       'limit': 200, 'page': page})
        response = get('/replays?' + query)
        games = response.get('replays', [])
        if not games:
            break
        for metadata in games:
            rid = metadata['_id']
            if rid in done or not eligible(metadata, args.pro):
                continue
            group = split_group(metadata)
            if group is None:
                continue
            attempts += 1
            raw = get('/replays/' + urllib.parse.quote(rid, safe='') + '/download', binary=True)
            source_hash = hashlib.sha256(raw).hexdigest()
            row = {'replay_id': rid, 'pro': args.pro, 'expected_code': who['code'],
                   'source_sha256': source_hash, 'metadata': metadata, 'group': group,
                   'split': split_of(group), 'extract_version': 2}
            if source_hash in hashes:
                row['status'] = 'duplicate'
            else:
                with tempfile.TemporaryDirectory(prefix='mu-verified-') as temp:
                    path = Path(temp) / (rid + '.slp')
                    path.write_bytes(raw)
                    data = extract.extract(path, args.pro, 0)
                if data is None:
                    row['status'] = 'rejected_identity_or_frames'
                else:
                    row['status'] = 'accepted'
                    row['frames'] = len(data['frame'])
                    row['identity_port'] = int(data['identity_port'])
                    data['split'] = np.array(row['split'])
                    data['split_group'] = np.array(group)
                    dest = frames / (rid + '.npz')
                    tmp = frames / (rid + '.part.npz')
                    np.savez_compressed(tmp, **data)
                    row['array_sha256'] = hashlib.sha256(tmp.read_bytes()).hexdigest()
                    tmp.replace(dest)
                    accepted += 1
            with manifest.open('a', encoding='utf-8') as log:
                log.write(json.dumps(row) + '\n')
            done.add(rid)
            hashes.add(source_hash)
            print(f'{args.pro}: {rid} {row["status"]}; {accepted} accepted', flush=True)
            if accepted >= args.games or attempts >= args.max_downloads:
                break
            time.sleep(8)
        if page >= response.get('pagination', {}).get('pages', page):
            break
        page += 1
        time.sleep(2.1)
    print(f'{args.pro}: stopped after {attempts} downloads, {accepted} verified games', flush=True)


if __name__ == '__main__':
    main()
