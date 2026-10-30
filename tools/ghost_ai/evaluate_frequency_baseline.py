"""Score held-out pro replays against a train-only, state-independent action prior."""
import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path

import numpy as np
import train
from players import PLAYERS

HOME = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def interval(values, rng, trials=2000):
    values = np.asarray(values, dtype=np.float64)
    if len(values) < 2:
        return [float(values.mean()), float(values.mean())]
    picks = rng.integers(0, len(values), size=(trials, len(values)))
    means = values[picks].mean(axis=1)
    return [float(np.quantile(means, .025)), float(np.quantile(means, .975))]


def identity_matches(data, record, pro, replay_id):
    method = str(data['identity_method'].item())
    if method == 'exact_connect_code_v1':
        valid = str(data['identity_code'].item()) == PLAYERS[pro]['code']
    elif method == 'linked_tournament_set_v1' and pro == 'hbox':
        valid = (str(data['identity_code'].item()) == '' and
                 str(data['identity_set_id'].item()) == str(record.get('set_id')) and
                 str(data['identity_set_game_replay_id'].item()) == replay_id and
                 str(data['identity_set_source'].item()) == 'startgg-export' and
                 str(data['identity_replay_display_name'].item()).casefold() == 'hungrybox')
    else:
        return False
    return (valid and record.get('identity_method', 'exact_connect_code_v1') == method and
            str(data['source_replay_id'].item()) == replay_id)


def checked_game(folder, record, pro):
    replay_id = str(record['replay_id'])
    path = folder / 'frames' / (replay_id + '.npz')
    if hashlib.sha256(path.read_bytes()).hexdigest() != record.get('array_sha256'):
        raise ValueError(f'{replay_id}: array hash differs from verified manifest')
    with np.load(path, allow_pickle=False) as source:
        data = {key: source[key] for key in source.files}
    if not identity_matches(data, record, pro, replay_id):
        raise ValueError(f'{replay_id}: replay identity differs from verified manifest')
    if str(data['split'].item()) != record.get('split'):
        raise ValueError(f'{replay_id}: array split differs from verified manifest')
    if str(data['split_group'].item()) != str(record.get('group')):
        raise ValueError(f'{replay_id}: array group differs from verified manifest')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pro', choices=list(PLAYERS), required=True)
    parser.add_argument('--split', choices=['development', 'test'], default='development')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    torch_threads = 1
    import torch
    torch.set_num_threads(torch_threads)

    folder = HOME / 'data' / args.pro / 'verified'
    records = [json.loads(line) for line in (folder / 'manifest.jsonl').read_text(encoding='utf-8').splitlines()]
    train_records = [row for row in records if row.get('status') == 'accepted' and row.get('split') == 'train']
    held_out = [row for row in records if row.get('status') == 'accepted' and row.get('split') == args.split]
    if not train_records or not held_out:
        raise SystemExit('Train and held-out replay splits must both be non-empty')

    stick_counts = np.zeros(train.ft.N_STICK, dtype=np.int64)
    cstick_counts = np.zeros(train.ft.N_CSTICK, dtype=np.int64)
    button_positives = np.zeros(5, dtype=np.int64)
    training_frames = 0
    for record in train_records:
        arrays = train.game_rows(checked_game(folder, record, args.pro), 0)
        if arrays is None:
            continue
        _, _, _, stick, cstick, buttons = arrays
        stick_counts += np.bincount(stick, minlength=len(stick_counts))
        cstick_counts += np.bincount(cstick, minlength=len(cstick_counts))
        button_positives += buttons.sum(axis=0).astype(np.int64)
        training_frames += len(stick)
    if training_frames == 0:
        raise SystemExit('No canonical training decision frames')

    alpha = 0.5
    stick_prior = (stick_counts + alpha) / (training_frames + alpha * len(stick_counts))
    cstick_prior = (cstick_counts + alpha) / (training_frames + alpha * len(cstick_counts))
    button_prior = (button_positives + alpha) / (training_frames + 2 * alpha)
    log_button_on = np.log(button_prior)
    log_button_off = np.log1p(-button_prior)

    games, grouped = [], defaultdict(list)
    total_nll, total_frames = 0., 0
    for record in sorted(held_out, key=lambda row: row['replay_id']):
        arrays = train.game_rows(checked_game(folder, record, args.pro), 0)
        if arrays is None:
            continue
        _, _, _, stick, cstick, buttons = arrays
        bits = buttons.astype(bool)
        per_frame = -np.log(stick_prior[stick]) - np.log(cstick_prior[cstick])
        per_frame -= np.where(bits, log_button_on, log_button_off).sum(axis=1)
        value = float(per_frame.mean())
        group = str(record.get('group') or record['replay_id'])
        games.append({'replay_id': str(record['replay_id']), 'split_group': group,
                      'frames': len(per_frame), 'factorized_action_nll': value})
        grouped[group].append(value)
        total_nll += float(per_frame.sum())
        total_frames += len(per_frame)
    if not games:
        raise SystemExit('No canonical held-out decision frames')

    rng = np.random.default_rng(90623)
    group_means = [float(np.mean(values)) for values in grouped.values()]
    game_values = [row['factorized_action_nll'] for row in games]
    report = {
        'pro': args.pro, 'split': args.split,
        'baseline': 'factorized marginal action frequencies from this pro training split; no state/history inputs',
        'training_manifest_sha256': hashlib.sha256((folder / 'manifest.jsonl').read_bytes()).hexdigest(),
        'training_games': len(train_records), 'training_decision_frames': training_frames,
        'games_evaluated': len(games), 'independent_split_groups': len(group_means),
        'total_sequential_decision_frames': total_frames,
        'mean_game_factorized_action_nll': float(np.mean(game_values)),
        'mean_game_factorized_action_nll_bootstrap95': interval(game_values, rng),
        'mean_group_factorized_action_nll': float(np.mean(group_means)),
        'mean_group_factorized_action_nll_bootstrap95': interval(group_means, rng),
        'frame_weighted_factorized_action_nll': total_nll / total_frames,
        'per_game': games,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'per_game'}, indent=2))


if __name__ == '__main__':
    main()
