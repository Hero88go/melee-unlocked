"""Estimate and request older exact-code replay ranges beyond the newest 10,000-game jobs."""
import argparse
import json
import os
from pathlib import Path
from datetime import date, timedelta
import urllib.error

from bulk_verified import call, HERE
from players import PLAYERS


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--create', action='store_true', help='create after each estimate is within the expected source range')
    ap.add_argument('--pros', nargs='*', choices=list(PLAYERS), default=['mang0', 'zain', 'cody', 'amsa'])
    ap.add_argument('--max-files', type=int, default=10000)
    ap.add_argument('--tail-number', type=int, default=1,
                    help='older-range sequence number; ranges after 1 start before the oldest accepted replay')
    args = ap.parse_args()
    if args.tail_number < 1:
        raise SystemExit('--tail-number must be at least 1')
    # Tail one starts before the initial newest-first bulk. Later tails begin before the oldest
    # accepted replay already on disk, so repeated bounded jobs continue backward without overlap.
    cutoffs = {'mang0': '2023-11-16', 'zain': '2022-01-23',
               'cody': '2023-09-05', 'amsa': '2023-05-15', 'hbox': '2020-01-01'}
    for pro in args.pros:
        job_file = HERE / f'job-{pro}-tail{args.tail_number}.json'
        failed_job = None
        if job_file.exists():
            existing = json.loads(job_file.read_text())
            try:
                status = call('/jobs/' + existing['jobId'])
            except (KeyError, urllib.error.HTTPError) as exc:
                print(json.dumps({'pro': pro, 'status_error': str(exc)[:300]}), flush=True)
                continue
            if status.get('status') not in ('failed', 'expired', 'cancelled'):
                print(json.dumps({'pro': pro, 'existing': status}), flush=True)
                continue
            failed_job = existing['jobId']
            if not args.create:
                print(json.dumps({'pro': pro, 'failed_job': failed_job,
                                  'status': status.get('status'), 'retry_requires_create': True}), flush=True)
                continue
        if args.tail_number > 1 or pro == 'hbox':
            manifest = HERE / 'data' / pro / 'verified' / 'manifest.jsonl'
            accepted_dates = []
            if manifest.is_file():
                for line in manifest.read_text(encoding='utf-8').splitlines():
                    row = json.loads(line)
                    group = row.get('group', '')
                    if row.get('status') == 'accepted' and str(group).startswith('day:'):
                        try:
                            accepted_dates.append(date.fromisoformat(group[4:]))
                        except ValueError:
                            pass
            if not accepted_dates:
                print(json.dumps({'pro': pro, 'tail': args.tail_number,
                                  'error': 'no accepted dated replay range exists to continue from'}), flush=True)
                continue
            cutoffs[pro] = (min(accepted_dates) - timedelta(days=1)).isoformat()
        person = PLAYERS[pro]
        body = {'p1ConnectCode': person['code'], 'p1CharacterId': str(person['css_character']),
                'endDate': cutoffs[pro], 'sort': 'startAt:-1', 'maxFiles': args.max_files}
        try:
            estimate = call('/replays/estimate', body)
        except urllib.error.HTTPError as exc:
            print(json.dumps({'pro': pro, 'estimate_error': exc.code,
                              'detail': exc.read().decode(errors='replace')[:300]}), flush=True)
            continue
        report = {'pro': pro, 'cutoff': cutoffs[pro], 'estimate': estimate}
        if not args.create or estimate.get('replayCount', 0) == 0:
            print(json.dumps(report), flush=True)
            continue
        try:
            created = call('/jobs', body)
        except urllib.error.HTTPError as exc:
            print(json.dumps({**report, 'create_error': exc.code,
                              'detail': exc.read().decode(errors='replace')[:300]}), flush=True)
            continue
        report['created'] = created
        if failed_job:
            report['replaces_failed_job'] = failed_job
        job_file.write_text(json.dumps(created, indent=2), encoding='utf-8')
        print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
