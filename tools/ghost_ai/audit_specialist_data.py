"""Verify every accepted specialist array against its manifest hash and replay identity."""
import hashlib
import json
import os
from pathlib import Path

import numpy as np
from players import PLAYERS

HOME = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def audit(pro):
    folder = HOME / 'data' / pro / 'verified'
    manifest = folder / 'manifest.jsonl'
    records = [json.loads(line) for line in manifest.read_text(encoding='utf-8').splitlines()] if manifest.exists() else []
    accepted = [r for r in records if r.get('status') == 'accepted']
    errors, totals = [], {'train': 0, 'development': 0, 'test': 0, 'frames': 0}
    group_splits, split_groups = {}, {'train': set(), 'development': set(), 'test': set()}
    seen = set()
    for record in accepted:
        rid = record.get('replay_id')
        path = folder / 'frames' / (str(rid) + '.npz')
        try:
            method = record.get('identity_method', 'exact_connect_code_v1')
            if record.get('pro') != pro:
                raise ValueError('manifest_pro_mismatch')
            if method == 'exact_connect_code_v1':
                if record.get('expected_code') != PLAYERS[pro]['code']:
                    raise ValueError('manifest_exact_code_mismatch')
            elif method == 'linked_tournament_set_v1' and pro == 'hbox':
                if (record.get('expected_code') is not None or record.get('set_source') != 'startgg-export' or
                        record.get('set_game_replay_id') != rid or
                        record.get('replay_display_name', '').casefold() != 'hungrybox' or
                        int(record.get('set_port', 0)) != int(record.get('expected_player_index', -2)) + 1):
                    raise ValueError('manifest_tournament_link_mismatch')
            else:
                raise ValueError('unapproved_identity_method')
            if rid in seen:
                raise ValueError('duplicate_accepted_replay_id')
            seen.add(rid)
            if not path.is_file():
                raise ValueError('missing_array')
            actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual_hash != record.get('array_sha256'):
                raise ValueError('array_sha256_mismatch')
            with np.load(path, allow_pickle=False) as data:
                code = str(data['identity_code'].item())
                method = str(data['identity_method'].item())
                if method != record.get('identity_method', 'exact_connect_code_v1'):
                    raise ValueError('array_manifest_method_mismatch')
                if method == 'exact_connect_code_v1':
                    if code != PLAYERS[pro]['code']:
                        raise ValueError('array_exact_identity_mismatch')
                elif method == 'linked_tournament_set_v1' and pro == 'hbox':
                    if (code or str(data['identity_set_id'].item()) != record.get('set_id') or
                            str(data['identity_set_game_replay_id'].item()) != str(rid) or
                            str(data['identity_set_source'].item()) != 'startgg-export' or
                            str(data['identity_replay_display_name'].item()).casefold() != 'hungrybox'):
                        raise ValueError('array_tournament_link_mismatch')
                if str(data['source_replay_id'].item()) != str(rid):
                    raise ValueError('replay_id_mismatch')
                frames = data['frame'].astype(np.int64)
                if len(frames) < 99 or np.any(np.diff(frames) != 1):
                    raise ValueError('invalid_canonical_frame_run')
                split = record.get('split')
                if split not in totals:
                    raise ValueError('invalid_split')
                group = str(record.get('group') or ('replay:' + str(rid)))
                prior_split = group_splits.setdefault(group, split)
                if prior_split != split:
                    raise ValueError('split_group_crosses_splits')
                split_groups[split].add(group)
                if str(data['split'].item()) != split:
                    raise ValueError('array_split_mismatch')
                if 'split_group' not in data or str(data['split_group'].item()) != group:
                    raise ValueError('array_split_group_mismatch')
                totals[split] += 1
                totals['frames'] += len(frames)
        except Exception as exc:
            errors.append({'replay_id': rid, 'error': type(exc).__name__ + ': ' + str(exc)[:160]})
    result = {'pro': pro, 'identity_code': PLAYERS[pro]['code'], 'manifest_sha256': hashlib.sha256(manifest.read_bytes()).hexdigest() if manifest.exists() else None,
              'accepted_rows': len(accepted), 'verified_unique_games': len(seen) - sum(e['error'].endswith('duplicate_accepted_replay_id') for e in errors),
              'split_games': {k: totals[k] for k in ('train', 'development', 'test')},
              'split_group_counts': {key: len(value) for key, value in split_groups.items()},
              'canonical_frames': totals['frames'], 'errors': errors}
    return result


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pros', nargs='*', choices=list(PLAYERS), default=list(PLAYERS))
    ap.add_argument('--out', type=Path, default=None)
    args = ap.parse_args()
    reports = [audit(pro) for pro in args.pros]
    payload = {'reports': reports, 'all_pass': all(not row['errors'] for row in reports)}
    out = args.out or HOME / 'runs' / 'specialist-data-audit.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, indent=2), encoding='utf-8')
    print(json.dumps([{k: v for k, v in row.items() if k != 'errors'} | {'error_count': len(row['errors'])} for row in reports], indent=2))
    if not payload['all_pass']:
        raise SystemExit('Specialist provenance/hash/frame audit failed; see ' + str(out))


if __name__ == '__main__':
    main()
