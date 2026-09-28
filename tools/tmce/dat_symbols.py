"""List the public symbols of HSD archives (.dat files): name, data offset, and the size up to the next
symbol. usage: python tools/tmce/dat_symbols.py FILE.dat [...]"""
import struct
import sys


def symbols(blob):
    file_size, data_size, reloc_n, pub_n, ext_n = struct.unpack('>5I', blob[:20])
    base = 0x20
    reloc_at = base + data_size
    pub_at = reloc_at + reloc_n * 4
    str_at = pub_at + (pub_n + ext_n) * 8
    out = []
    for i in range(pub_n):
        off, name_off = struct.unpack('>2I', blob[pub_at + i * 8:pub_at + i * 8 + 8])
        end = blob.index(b'\0', str_at + name_off)
        out.append((blob[str_at + name_off:end].decode('latin-1'), off))
    return data_size, out


def main():
    for path in sys.argv[1:]:
        blob = open(path, 'rb').read()
        data_size, syms = symbols(blob)
        print('%s: %d bytes, data %d' % (path, len(blob), data_size))
        offs = sorted(o for _, o in syms) + [data_size]
        for name, off in syms:
            nxt = min(o for o in offs if o > off) if any(o > off for o in offs) else data_size
            print('  %-24s 0x%06x (%d bytes)' % (name, off, nxt - off))


if __name__ == '__main__':
    main()
