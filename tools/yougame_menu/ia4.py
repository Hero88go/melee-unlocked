def decode_ia4(raw, w, h):
    """GX IA4: 8x4 tiles, one byte per texel, high nibble alpha, low nibble intensity. Returns RGBA bytes."""
    out = bytearray(w * h * 4); i = 0
    for ty in range(0, h, 4):
        for tx in range(0, w, 8):
            for y in range(4):
                for x in range(8):
                    v = raw[i]; i += 1
                    px, py = tx + x, ty + y
                    if px < w and py < h:
                        a = (v >> 4) * 17; l = (v & 15) * 17
                        o = (py * w + px) * 4; out[o:o + 4] = bytes((l, l, l, a))
    return bytes(out)

def encode_ia4(rgba, w, h):
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 8):
            for y in range(4):
                for x in range(8):
                    px, py = tx + x, ty + y
                    if px < w and py < h:
                        o = (py * w + px) * 4; r, g, b, a = rgba[o:o + 4]
                        l = (r * 299 + g * 587 + b * 114) // 1000
                        out.append(((a * 15 + 127) // 255) << 4 | ((l * 15 + 127) // 255))
                    else:
                        out.append(0)
    return bytes(out)
