"""Download new crash and log reports from the relay (tools/crash_relay) to a local folder.

Usage: py -3.12 tools/pull_reports.py [--out C:/Games/reports] [--all] [--limit N]

Each report lands in <out>/<id>/report.md, plus session.trace when the player sent one. The newest
key pulled is remembered in <out>/.last_key, so the next run only fetches what came in since.
The admin token is read from MELEE_REPORTS_TOKEN or ~/.melee_reports_token (never from the repo).
"""
from pathlib import Path
import argparse
import json
import os
import sys
import urllib.parse
import urllib.request

RELAY = 'https://melee-crash-relay.firescribe-share-worker.workers.dev'


def token():
    value = os.environ.get('MELEE_REPORTS_TOKEN')
    if value:
        return value.strip()
    path = Path.home() / '.melee_reports_token'
    if path.exists():
        return path.read_text().strip()
    sys.exit('no admin token: set MELEE_REPORTS_TOKEN or write ~/.melee_reports_token')


def fetch(path, tok):
    request = urllib.request.Request(RELAY + path, headers={'Authorization': 'Bearer ' + tok,
                                                            'User-Agent': 'pull_reports'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, default=Path('C:/Games/reports'))
    ap.add_argument('--all', action='store_true', help='ignore the remembered position')
    ap.add_argument('--limit', type=int, default=200)
    args = ap.parse_args()
    tok = token()
    args.out.mkdir(parents=True, exist_ok=True)
    state = args.out / '.last_key'
    after = '' if args.all or not state.exists() else state.read_text().strip()
    items = json.loads(fetch('/admin/list?after=' + urllib.parse.quote(after), tok))['items'][:args.limit]
    if not items:
        print('no new reports')
        return 0
    for item in items:
        key, meta = item['key'], item.get('meta', {})
        folder = args.out / key[2:]
        folder.mkdir(exist_ok=True)
        q = urllib.parse.quote(key)
        (folder / 'report.md').write_bytes(fetch('/admin/get?key=' + q, tok))
        if meta.get('trace'):
            (folder / 'session.trace').write_bytes(fetch('/admin/trace?key=' + q, tok))
        note = (meta.get('note') or '').replace('\n', ' ')[:120]
        print(f"{key[2:]}  {meta.get('kind', '?'):5}  {meta.get('version', '?'):8}  {meta.get('engine', '?'):13}  "
              f"{(meta.get('where') or '')[:70]}{'  | ' + note if note else ''}")
        state.write_text(key)
    print(f'{len(items)} report(s) in {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
