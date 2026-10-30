"""Score an independent pro policy against held-out human replay decisions, with no game runs."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
import train
from train_specialist import load_games

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pro', required=True)
    ap.add_argument('--checkpoint', type=Path, required=True)
    ap.add_argument('--split', choices=['development', 'test'], default='development')
    ap.add_argument('--device', default='cuda')
    ap.add_argument('--batches', type=int, default=64)
    ap.add_argument('--games', type=int, default=200)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    torch.set_num_threads(2)
    train.DEV = args.device; train.B = 64
    folder = HERE / 'data' / args.pro / 'verified'
    records = {r['replay_id']: r for r in (json.loads(line) for line in (folder/'manifest.jsonl').read_text().splitlines())}
    files = sorted(folder/'frames'/(rid+'.npz') for rid, r in records.items()
                   if r['status']=='accepted' and r['split']==args.split)
    rng = np.random.default_rng(11981)
    if len(files) > args.games:
        files = [files[i] for i in rng.choice(len(files), args.games, replace=False)]
    if not files:
        raise SystemExit('No eligible held-out games')
    def games():
        for data, index, name in load_games(files, args.pro, records):
            # Data's sampler uses a boolean for the development pool; map only this selected split.
            data['split'] = np.array('development')
            yield data, index, name
    data = train.Data(games=games())
    checkpoint = args.checkpoint.read_bytes()
    state = torch.load(io.BytesIO(checkpoint), map_location=args.device, weights_only=True)
    net = train.Net(n_pros=state['pro.weight'].shape[0]).to(args.device).eval()
    net.load_state_dict(state)
    n, nll, correct_s, correct_c, brier = 0, 0., 0, 0, 0.
    pred_freq, true_freq = np.zeros(5), np.zeros(5)
    conditional = {}
    with torch.no_grad():
        for _ in range(args.batches):
            cont, flags, cat, stick, cstick, buttons, _ = data.batch(rng, val=True)
            ls, lc, lb, _ = net(cont, flags, cat)
            sl = slice(train.BURN, None)
            s, c, b = stick[:,sl], cstick[:,sl], buttons[:,sl]
            a = F.cross_entropy(ls[:,sl].flatten(0,1), s.flatten(), reduction='none').reshape(s.shape)
            a += F.cross_entropy(lc[:,sl].flatten(0,1), c.flatten(), reduction='none').reshape(c.shape)
            a += F.binary_cross_entropy_with_logits(lb[:,sl], b, reduction='none').sum(-1)
            p = lb[:,sl].sigmoid()
            count = s.numel(); n += count; nll += a.sum().item()
            correct_s += (ls[:,sl].argmax(-1)==s).sum().item()
            correct_c += (lc[:,sl].argmax(-1)==c).sum().item()
            brier += ((p-b)**2).sum().item()
            pred_freq += p.sum((0,1)).cpu().numpy(); true_freq += b.sum((0,1)).cpu().numpy()
            for dimension, values in [('opponent_character',cat[:,sl,3]),('stage',cat[:,sl,4])]:
                for value in values.unique():
                    key = dimension+':'+str(value.item()); mask = values==value
                    record = conditional.setdefault(key, {'frames':0,'joint_nll_sum':0.})
                    record['frames'] += mask.sum().item(); record['joint_nll_sum'] += a[mask].sum().item()
    report = {'pro':args.pro,'split':args.split,'checkpoint_sha256':hashlib.sha256(checkpoint).hexdigest(),
              'games_sampled':len(files),'sequences_sampled':args.batches*train.B,'decision_frames_sampled':n,
              'joint_action_nll':nll/n,'stick_accuracy':correct_s/n,'cstick_accuracy':correct_c/n,
              'button_brier':brier/(n*5),'predicted_button_frequency':(pred_freq/n).tolist(),
              'human_button_frequency':(true_freq/n).tolist(),
              'conditional':{key:{'frames':r['frames'],'joint_action_nll':r['joint_nll_sum']/r['frames']}
                             for key,r in conditional.items()},
              'limitations':['Samples overlap within games; frame count is not independent trial count.',
                             'Replay likelihood measures prediction, not live strength or exact imitation.']}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items() if k!='conditional'},indent=2))


if __name__ == '__main__':
    main()
