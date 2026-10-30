"""Train an independent policy using only one verified pro's demonstrations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time

import numpy as np
import torch
import torch.nn.functional as F

import train
from players import PLAYERS

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def load_games(files, pro=None, records=None):
    for file in files:
        if records is not None:
            record = records.get(file.stem)
            if not record or record.get('status') != 'accepted':
                raise ValueError(f'{file.name}: no accepted replay manifest row')
            digest = hashlib.sha256(file.read_bytes()).hexdigest()
            if digest != record.get('array_sha256'):
                raise ValueError(f'{file.name}: array hash does not match the provenance manifest')
        with np.load(file, allow_pickle=False) as source:
            game = {k: source[k] for k in source.files}
        if pro is not None:
            method = str(game['identity_method'].item())
            record = records.get(file.stem) if records is not None else None
            if method == 'exact_connect_code_v1':
                if str(game['identity_code'].item()) != PLAYERS[pro]['code']:
                    raise ValueError(f'{file.name}: replay identity differs from selected pro')
            elif method == 'linked_tournament_set_v1' and pro == 'hbox':
                checks = (
                    str(game['identity_code'].item()) == '',
                    str(game['identity_set_id'].item()) == str(record.get('set_id')),
                    str(game['identity_set_game_replay_id'].item()) == file.stem,
                    str(game['identity_set_source'].item()) == 'startgg-export',
                    str(game['identity_replay_display_name'].item()).casefold() == 'hungrybox',
                    int(record.get('set_port', 0)) == int(record.get('expected_player_index', -2)) + 1,
                )
                if not all(checks):
                    raise ValueError(f'{file.name}: linked tournament attribution is inconsistent')
            else:
                raise ValueError(f'{file.name}: unsupported or unverified identity method {method!r}')
            if record is not None and record.get('identity_method', method) != method:
                raise ValueError(f'{file.name}: array identity method differs from its manifest')
            if str(game['source_replay_id'].item()) != file.stem:
                raise ValueError(f'{file.name}: source replay id differs from its filename')
        yield game, 0, file.stem


def objective(net, batch):
    cont, flags, cat, stick, cstick, btn, mirror = batch
    cont, stick, cstick = train.mirror_batch(cont, stick, cstick, mirror)
    ls, lc, lb, _ = net(cont, flags, cat)
    sl = slice(train.BURN, None)
    a = F.cross_entropy(ls[:, sl].flatten(0, 1), stick[:, sl].flatten())
    b = F.cross_entropy(lc[:, sl].flatten(0, 1), cstick[:, sl].flatten())
    # Plain Bernoulli likelihood preserves the pro's real button frequency. Positive weighting
    # changes its target probabilities and must not silently define a faithful controller model.
    c = F.binary_cross_entropy_with_logits(lb[:, sl], btn[:, sl], reduction='none').sum(-1).mean()
    return a + b + c, (a.detach().item(), b.detach().item(), c.detach().item())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pro', choices=list(PLAYERS), required=True)
    ap.add_argument('--minutes', type=float, default=60)
    ap.add_argument('--games-per-chunk', type=int, default=400)
    ap.add_argument('--steps-per-chunk', type=int, default=800)
    ap.add_argument('--evaluate-every', type=int, default=250)
    ap.add_argument('--patience', type=int, default=20)
    ap.add_argument('--seed', type=int, default=4201)
    ap.add_argument('--learning-rate', type=float, default=0.001)
    ap.add_argument('--weight-decay', type=float, default=0.0001)
    ap.add_argument('--output-dir', type=Path, default=None,
                    help='separate candidate directory for reproducible multi-seed comparisons')
    ap.add_argument('--resume', action='store_true')
    args = ap.parse_args()
    if args.learning_rate <= 0 or args.weight_decay < 0:
        raise SystemExit('learning rate must be positive and weight decay nonnegative')
    torch.set_num_threads(4)
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    folder = HERE / 'data' / args.pro / 'verified'
    records = {r['replay_id']: r for r in (json.loads(line) for line in (folder/'manifest.jsonl').read_text().splitlines())}
    by_split = {key: [] for key in ('train', 'development', 'test')}
    for rid, record in records.items():
        if record['status'] != 'accepted':
            continue
        method = record.get('identity_method', 'exact_connect_code_v1')
        if method == 'exact_connect_code_v1' and record.get('expected_code') != PLAYERS[args.pro]['code']:
            continue
        if method == 'linked_tournament_set_v1':
            if (args.pro != 'hbox' or record.get('set_source') != 'startgg-export' or
                    record.get('set_game_replay_id') != rid or record.get('replay_display_name', '').casefold() != 'hungrybox' or
                    int(record.get('set_port', 0)) != int(record.get('expected_player_index', -2)) + 1):
                continue
        elif method != 'exact_connect_code_v1':
            continue
        file = folder / 'frames' / (rid + '.npz')
        if file.exists():
            by_split[record['split']].append(file)
    if len(by_split['train']) < 20 or len(by_split['development']) < 5:
        raise SystemExit('Need at least 20 verified train games and 5 development games from separate groups')
    output = args.output_dir or (HERE / 'specialists' / args.pro)
    output.mkdir(parents=True, exist_ok=True)
    manifest_hash = hashlib.sha256((folder/'manifest.jsonl').read_bytes()).hexdigest()
    methods = {}
    for split_files in by_split.values():
        for file in split_files:
            method = records[file.stem].get('identity_method', 'exact_connect_code_v1')
            methods[method] = methods.get(method, 0) + 1
    metadata = {'pro': args.pro, 'identity_code': PLAYERS[args.pro]['code'], 'architecture': 'gru-v1-independent',
                'training_objective': 'stick CE + cstick CE + summed independent button BCE (joint action NLL)',
                'initialization': 'random_from_scratch', 'named_pro_only': True, 'teacher_used': False,
                'general_ranked_used': False, 'source_manifest_sha256': manifest_hash,
                'games': {key: len(values) for key, values in by_split.items()},
                'identity_methods': methods,
                'final_test_used': False, 'seed': args.seed,
                'learning_rate': args.learning_rate, 'weight_decay': args.weight_decay,
                'output_dir': str(output)}
    (output / 'training-manifest.json').write_text(json.dumps(metadata, indent=2))
    dev_files = sorted(by_split['development'])
    # Deterministic subset for frequent checks; full development scoring follows candidate selection.
    if len(dev_files) > 80:
        dev_files = [dev_files[i] for i in rng.choice(len(dev_files), 80, replace=False)]
    development = train.Data(games=load_games(dev_files, args.pro, records))
    net = train.Net(n_pros=1).to(train.DEV)
    opt = torch.optim.AdamW(net.parameters(), lr=args.learning_rate, weight_decay=args.weight_decay)
    checkpoint = output / 'model.pt'
    if args.resume and checkpoint.exists():
        net.load_state_dict(torch.load(checkpoint, weights_only=True, map_location=train.DEV))
    step, best, stale = 0, float('inf'), 0
    started = time.monotonic()
    files = list(by_split['train'])
    data = None
    cursor, seen = len(files), set()
    with (output / 'train.jsonl').open('a', encoding='utf-8') as log:
        while time.monotonic() - started < args.minutes * 60:
            if step % args.steps_per_chunk == 0:
                if data is not None:
                    del data
                if cursor >= len(files):
                    rng.shuffle(files); cursor = 0
                chunk = files[cursor:cursor+args.games_per_chunk]
                cursor += len(chunk)
                seen.update(file.stem for file in chunk)
                data = train.Data(games=load_games(chunk, args.pro, records))
            net.train()
            loss, metrics = objective(net, data.batch(rng))
            opt.zero_grad(set_to_none=True); loss.backward()
            torch.nn.utils.clip_grad_norm_(net.parameters(), 1)
            opt.step(); step += 1
            if step % args.evaluate_every == 0:
                net.eval()
                with torch.no_grad():
                    values = [objective(net, development.batch(np.random.default_rng(100+i), val=True))[0].item()
                              for i in range(12)]
                value = float(np.mean(values))
                row = {'step': step, 'seconds': time.monotonic()-started, 'train_loss': sum(metrics),
                       'development_loss': value, 'distinct_train_games_loaded': len(seen)}
                if value < best - 0.0001:
                    best = value; stale = 0
                    tmp = checkpoint.with_suffix('.part'); torch.save(net.state_dict(), tmp); tmp.replace(checkpoint)
                    row['saved_best'] = True
                else:
                    stale += 1
                print(args.pro, json.dumps(row), flush=True); log.write(json.dumps(row)+'\n'); log.flush()
                if stale >= args.patience and len(seen) == len(files):
                    print(args.pro, 'development plateau after complete corpus exposure', flush=True)
                    break
        metadata.update(steps=step, seconds=time.monotonic()-started, best_development_loss=best,
                        distinct_train_games_loaded=len(seen), complete_corpus_exposure=len(seen)==len(files))
        (output / 'training-manifest.json').write_text(json.dumps(metadata, indent=2))


if __name__ == '__main__':
    main()
