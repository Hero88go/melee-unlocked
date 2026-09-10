"""Read selected assets from the user's ISO; never patch the disc or distribute assets."""
import hashlib
import json
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
iso = Path(json.loads((root / 'reports/baseline.json').read_text())['iso'])
destination = root / 'runtime/native-assets'
destination.mkdir(exist_ok=True)
report = {}
with iso.open('rb') as disc:
    disc.seek(0x424)
    fst_offset, fst_size = struct.unpack('>II', disc.read(8))
    if fst_offset + fst_size > iso.stat().st_size or fst_size > 32*1024*1024:
        raise ValueError('invalid disc FST')
    disc.seek(fst_offset)
    fst = disc.read(fst_size)
    count = struct.unpack_from('>I', fst, 8)[0]
    if count*12 > len(fst):
        raise ValueError('invalid FST entry count')
    for index in range(1, count):
        kind_name, offset, size = struct.unpack_from('>III', fst, index*12)
        if kind_name >> 24:
            continue
        name_start = count*12 + (kind_name & 0xffffff)
        name = fst[name_start:fst.index(b'\0', name_start)].decode('ascii')
        if name not in ('PlFcNr.dat', 'PlFcAJ.dat'):
            continue
        if offset + size > iso.stat().st_size:
            raise ValueError('asset outside disc')
        disc.seek(offset)
        data = disc.read(size)
        (destination / name).write_bytes(data)
        info = {'bytes': size, 'sha256': hashlib.sha256(data).hexdigest(), 'archives': []}
        # Animation bundles contain consecutive HSD archives padded to 32-byte boundaries.
        position = 0
        while position + 32 <= len(data):
            length, data_size, reloc, public, external = struct.unpack_from('>5I', data, position)
            tables_end = 32 + data_size + reloc*4 + (public+external)*8
            if length < tables_end or length == 0 or position+length > len(data):
                info['unparsed_offset'] = position
                break
            symbols = []
            table = position+32+data_size+reloc*4
            for entry in range(public):
                object_offset, string_offset = struct.unpack_from('>II', data, table+entry*8)
                string_start = position+tables_end+string_offset
                if object_offset >= data_size or string_start >= position+length:
                    raise ValueError('invalid archive public symbol')
                end = data.index(b'\0', string_start, position+length)
                symbols.append({'name': data[string_start:end].decode('ascii'), 'data_offset': object_offset})
            info['archives'].append({'offset': position, 'length': length, 'public': symbols})
            position += (length+31) & ~31
        report[name] = info
if set(report) != {'PlFcNr.dat', 'PlFcAJ.dat'}:
    raise ValueError('required Falco assets not found')
(root / 'reports/native-assets.json').write_text(json.dumps(report, indent=2))
for name, info in report.items():
    print(name, info['bytes'], 'bytes;', len(info['archives']), 'HSD archives;',
          'unparsed offset:', info.get('unparsed_offset'))
