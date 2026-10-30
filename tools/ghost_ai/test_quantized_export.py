"""Exercise float/int8 export through the real native loader for one, four and five policies."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import numpy as np
import torch
import export
import features as ft
import train


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-exe', type=Path, required=True)
    args = parser.parse_args()
    exe = str(args.test_exe.resolve())
    torch.manual_seed(519)
    means = np.array([ft.stick_value(i) for i in range(ft.N_STICK)], np.float32)
    results = []
    with tempfile.TemporaryDirectory(prefix='mu-quant-export-') as temporary:
        folder = Path(temporary)
        for n in (1, 4, 5):
            model = train.Net(n_pros=n).state_dict()
            plain, compact = folder / f'float-{n}.bin', folder / f'int8-{n}.bin'
            reconstructed = export.write_model(compact, model, quantized=True, means=means)
            export.write_model(plain, model, means=means)
            for path in (plain, compact):
                subprocess.run([exe, str(path)], check=True, capture_output=True)
                header = struct.unpack('<4s13I', path.read_bytes()[:56])
                assert header[9] == n, (header, n)
            assert compact.stat().st_size < plain.stat().st_size * 0.27
            assert compact.stat().st_size < 1_650_000
            for name, original in model.items():
                restored = reconstructed[name]
                if original.ndim == 1:
                    assert torch.equal(original, restored), name
                else:
                    scales, _ = export.quantize_rows(original.numpy())
                    error = np.max(np.abs(original.numpy() - restored.numpy()), axis=1)
                    assert np.all(error <= scales * 0.5001 + 1e-7), name
            # Native exact-length checks must also reject corrupted exporter output.
            broken = folder / 'broken.bin'
            broken.write_bytes(compact.read_bytes()[:-1])
            assert subprocess.run([exe, str(broken)], capture_output=True).returncode != 0
            broken.write_bytes(compact.read_bytes() + b'\0')
            assert subprocess.run([exe, str(broken)], capture_output=True).returncode != 0
            results.append({'policies': n, 'float_bytes': plain.stat().st_size,
                            'int8_bytes': compact.stat().st_size,
                            'native_load': 'pass', 'biases_and_stick_means': 'float32'})
        # Invalid data must not replace an existing valid model.
        bad = train.Net(n_pros=1).state_dict()
        bad['inp.weight'][0, 0] = float('nan')
        marker = folder / 'preserved.bin'; marker.write_bytes(b'preserved')
        try:
            export.write_model(marker, bad, quantized=True, means=means)
            raise AssertionError('non-finite weights accepted')
        except ValueError:
            pass
        assert marker.read_bytes() == b'preserved'
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
