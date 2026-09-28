"""Show the MexTK members and the decomp rows around a console offset, to see why a twin member was left out.

usage: python tools/tmce/twin_debug.py <decomp_info.json> <MexTK type> <console offset hex> [span hex]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mextk_layout as M  # noqa: E402

info = json.load(open(sys.argv[1]))
mex = sys.argv[2]
off = int(sys.argv[3], 16) * 8
span = int(sys.argv[4], 16) * 8 if len(sys.argv) > 4 else 64
pairs = json.load(open(os.path.join(HERE, 'type_pairs.json')))
model = M.Model(M.preprocess(os.path.join(os.path.dirname(os.path.dirname(HERE)), 'run-source', 'tmce-src', 'MexTK'),
                             os.path.join(HERE, 'fakeinc')))
print('MexTK', mex)
for m in model.flatten(mex):
    if off - 32 <= m['cbit'] < off + span:
        print('  c%5x.%d w%-4d %-4s %-40s %s' % (m['cbit'] // 8, m['cbit'] % 8, m['cbits'], m['kind'], m['path'], m['type']))
dt = pairs[mex]
print('decomp', dt)
for r in info['types'][dt]['members']:
    if off - 32 <= r['cbit'] < off + span:
        print('  c%5x.%d w%-4d n%5x.%d w%-4d %-4s %-40s %s' % (r['cbit'] // 8, r['cbit'] % 8, r['cbits'], r['nbit'] // 8,
                                                          r['nbit'] % 8, r['nbits'], r['kind'], r['path'], r['type']))
