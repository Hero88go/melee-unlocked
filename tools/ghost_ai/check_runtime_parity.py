"""Build real-replay recurrent reference outputs with PyTorch and check native inference."""
import argparse
import os
import struct
import subprocess
from pathlib import Path

import numpy as np
import torch
import features as ft
import train
from extract import canonical_indices

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-exe', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--checkpoint', type=Path, default=HERE / 'model.pt')
    args = parser.parse_args()
    sd = torch.load(args.checkpoint, map_location='cpu', weights_only=True)
    net = train.Net(n_pros=sd['pro.weight'].shape[0]).eval()
    net.load_state_dict(sd)
    out = HERE / 'runs' / 'pro-fidelity-plan' / 'runtime-parity.bin'
    count = 64
    with out.open('wb') as f:
        f.write(struct.pack('<I', count * len(ft.PROS)))
        for pro_index, pro in enumerate(ft.PROS):
            file = next((HERE / 'data' / pro / 'frames').glob('*.npz'))
            with np.load(file) as source:
                indices = canonical_indices(source['frame'])
                n = len(source['frame'])
                data = {k: (v[indices] if v.ndim and len(v) == n else v) for k, v in source.items()}
            rows = train.game_rows(data, pro_index)
            cont, flags, cat = rows[:3]
            h = None
            for t in range(count):
                # Match live input scaling, using full float32 rather than training-storage float16.
                c = np.concatenate([
                    np.clip(np.nan_to_num(data[prefix+'num'][t+1].astype(np.float32)) * ft.NUM_SCALE, -6, 6)
                    for prefix in ('s_', 'o_')
                ] + [np.array([(data['o_num'][t+1,0].astype(float)-data['s_num'][t+1,0].astype(float))/100,
                               (data['o_num'][t+1,1].astype(float)-data['s_num'][t+1,1].astype(float))/100,
                               abs(data['o_num'][t+1,0].astype(float)-data['s_num'][t+1,0].astype(float))/100]),
                     ft.pad_block(data['s_pad'][t:t+2], data['s_btn'][t:t+2])[::-1].reshape(-1)]).astype(np.float32)
                with torch.no_grad():
                    *_, h = net(torch.from_numpy(c)[None,None], torch.from_numpy(flags[t])[None,None],
                                torch.from_numpy(cat[t].astype(np.int64))[None,None], h)
                obs = struct.pack('<BBBBhhI', ft.VERSION, pro_index, pro_index, 0, int(data['stage']), 0, t+100)
                for prefix in ('s_', 'o_'):
                    nums = np.nan_to_num(data[prefix+'num'][t+1].astype(np.float32))
                    obs += struct.pack('<16fHB5B', *nums, int(data[prefix+'state'][t+1]),
                                       int(data[prefix+'char'][t+1]), *data[prefix+'flags'][t+1])
                obs += struct.pack('<6fI', 0,0,0,0,0,0,0)
                assert len(obs) == 184
                f.write(obs)
                f.write(c[-20:].tobytes())
                f.write(h.numpy().astype('<f4').tobytes())
    subprocess.run([str(args.test_exe.resolve()), str(args.binary.resolve()), str(out.resolve())], check=True)


if __name__ == '__main__':
    main()
