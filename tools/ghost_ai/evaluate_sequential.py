"""Evaluate each held-out replay from its first frame, carrying GRU state continuously."""
import argparse
from collections import defaultdict
import hashlib
import io
import json
import os
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
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


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pro', required=True)
    ap.add_argument('--checkpoint', type=Path, required=True)
    ap.add_argument('--split', choices=['development', 'test'], default='test')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--device', default='cuda')
    ap.add_argument('--chunk', type=int, default=512)
    args = ap.parse_args()
    torch.set_num_threads(2)
    folder = HOME / 'data' / args.pro / 'verified'
    records = {r['replay_id']: r for r in (json.loads(line) for line in (folder / 'manifest.jsonl').read_text().splitlines())}
    selected = sorted((rid, r) for rid, r in records.items()
                      if r.get('status') == 'accepted' and r.get('split') == args.split)
    if not selected:
        raise SystemExit('No accepted games for held-out split')
    raw_checkpoint = args.checkpoint.read_bytes()
    state = torch.load(io.BytesIO(raw_checkpoint), map_location=args.device, weights_only=True)
    net = train.Net(n_pros=state['pro.weight'].shape[0]).to(args.device).eval()
    net.load_state_dict(state)
    games = []
    group_nll = defaultdict(list)
    total = {'frames': 0, 'nll': 0., 'stick_correct': 0, 'cstick_correct': 0, 'button_brier': 0.}
    button_pred, button_true = np.zeros(5), np.zeros(5)
    with torch.inference_mode():
        for rid, record in selected:
            file = folder / 'frames' / (rid + '.npz')
            if hashlib.sha256(file.read_bytes()).hexdigest() != record.get('array_sha256'):
                raise ValueError(f'{rid}: held-out array hash does not match its provenance manifest')
            with np.load(file, allow_pickle=False) as source:
                d = {k: source[k] for k in source.files}
            method = str(d['identity_method'].item())
            valid_identity = (method == 'exact_connect_code_v1' and
                              str(d['identity_code'].item()) == PLAYERS[args.pro]['code'])
            if method == 'linked_tournament_set_v1' and args.pro == 'hbox':
                valid_identity = (str(d['identity_code'].item()) == '' and
                                  str(d['identity_set_id'].item()) == str(record.get('set_id')) and
                                  str(d['identity_set_game_replay_id'].item()) == rid and
                                  str(d['identity_set_source'].item()) == 'startgg-export' and
                                  str(d['identity_replay_display_name'].item()).casefold() == 'hungrybox')
            if (not valid_identity or record.get('identity_method', 'exact_connect_code_v1') != method or
                    str(d['source_replay_id'].item()) != rid):
                raise ValueError(f'{rid}: held-out replay identity differs from its manifest')
            arrays = train.game_rows(d, 0)
            if arrays is None:
                continue
            cont, flags, cat, stick, cstick, btn = arrays
            h = None
            stat = {'replay_id': rid, 'split_group': str(record.get('group') or rid),
                    'frames': len(stick), 'nll_sum': 0., 'stick_correct': 0,
                    'cstick_correct': 0, 'button_brier': 0.}
            for start in range(0, len(stick), args.chunk):
                end = min(start + args.chunk, len(stick))
                c = torch.from_numpy(cont[start:end][None].astype(np.float32)).to(args.device)
                fl = torch.from_numpy(flags[start:end][None]).to(args.device)
                ct = torch.from_numpy(cat[start:end][None].astype(np.int64)).to(args.device)
                sy = torch.from_numpy(stick[start:end][None].astype(np.int64)).to(args.device)
                cy = torch.from_numpy(cstick[start:end][None].astype(np.int64)).to(args.device)
                by = torch.from_numpy(btn[start:end][None].astype(np.float32)).to(args.device)
                ls, lc, lb, h = net(c, fl, ct, h)
                per = F.cross_entropy(ls.flatten(0, 1), sy.flatten(), reduction='none').reshape(sy.shape)
                per += F.cross_entropy(lc.flatten(0, 1), cy.flatten(), reduction='none').reshape(cy.shape)
                per += F.binary_cross_entropy_with_logits(lb, by, reduction='none').sum(-1)
                probability = lb.sigmoid()
                count = end - start
                stat['nll_sum'] += per.sum().item()
                stat['stick_correct'] += (ls.argmax(-1) == sy).sum().item()
                stat['cstick_correct'] += (lc.argmax(-1) == cy).sum().item()
                stat['button_brier'] += ((probability - by) ** 2).sum().item()
                total['frames'] += count
                total['nll'] += per.sum().item()
                total['stick_correct'] += stat['stick_correct'] - stat.get('_last_s', 0)
                total['cstick_correct'] += stat['cstick_correct'] - stat.get('_last_c', 0)
                total['button_brier'] += ((probability - by) ** 2).sum().item()
                stat['_last_s'] = stat['stick_correct']; stat['_last_c'] = stat['cstick_correct']
                button_pred += probability.sum((0, 1)).cpu().numpy()
                button_true += by.sum((0, 1)).cpu().numpy()
            count = stat.pop('frames')
            stat.pop('_last_s', None); stat.pop('_last_c', None)
            stat['joint_action_nll'] = stat.pop('nll_sum') / count
            stat['stick_accuracy'] = stat.pop('stick_correct') / count
            stat['cstick_accuracy'] = stat.pop('cstick_correct') / count
            stat['button_brier'] /= count * 5
            group_nll[stat['split_group']].append(stat['joint_action_nll'])
            games.append(stat)
    if not games:
        raise SystemExit('All candidate games were too short or had missing frame runs')
    rng = np.random.default_rng(90623)
    group_means = [float(np.mean(values)) for values in group_nll.values()]
    report = {
        'pro': args.pro, 'split': args.split,
        'checkpoint_sha256': hashlib.sha256(raw_checkpoint).hexdigest(),
        'games_evaluated': len(games),
        'independent_split_groups': len(group_means),
        'total_sequential_decision_frames': total['frames'],
        'mean_game_joint_action_nll': float(np.mean([g['joint_action_nll'] for g in games])),
        'mean_game_joint_action_nll_bootstrap95': interval([g['joint_action_nll'] for g in games], rng),
        'mean_group_joint_action_nll': float(np.mean(group_means)),
        'mean_group_joint_action_nll_bootstrap95': interval(group_means, rng),
        'mean_game_stick_accuracy': float(np.mean([g['stick_accuracy'] for g in games])),
        'mean_game_cstick_accuracy': float(np.mean([g['cstick_accuracy'] for g in games])),
        'mean_game_button_brier': float(np.mean([g['button_brier'] for g in games])),
        'frame_weighted_joint_action_nll': total['nll'] / total['frames'],
        'frame_weighted_stick_accuracy': total['stick_correct'] / total['frames'],
        'frame_weighted_cstick_accuracy': total['cstick_correct'] / total['frames'],
        'predicted_button_frequency': (button_pred / total['frames']).tolist(),
        'human_button_frequency': (button_true / total['frames']).tolist(),
        'per_game': games,
        'limitations': [
            'Group-level bootstrap uses the recorded day or tournament split group; tournaments can still share a play style.',
            'Behavioral prediction on recorded states does not establish strength in interactive matches.',
        ],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'per_game'}, indent=2))


if __name__ == '__main__':
    main()
