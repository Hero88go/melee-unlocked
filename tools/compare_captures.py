"""Compare diagnostic GPU readbacks; differences alone do not prove visual fidelity."""
import hashlib
import itertools
import json
from pathlib import Path
from PIL import Image, ImageChops

root = Path(__file__).resolve().parents[1]
directory = root / 'reports/frame-captures'
names = ['source-previous', 'quarter', 'three-quarter', 'source-current']
images = {}
report = {'limitation': 'Pixel differences prove distinct images, not correct intermediate poses or latency.'}
for name in names:
    path = directory / (name + '.ppm')
    images[name] = Image.open(path).convert('RGB')
    images[name].save(directory / (name + '.png'))
    report[name] = {'size': images[name].size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
for a, b in itertools.combinations(names, 2):
    difference = ImageChops.difference(images[a], images[b])
    report[a + ' vs ' + b] = {'changed_bbox': difference.getbbox(),
                              'different_pixels': sum(pixel != (0, 0, 0) for pixel in difference.getdata())}
(root / 'reports/frame-comparison.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
