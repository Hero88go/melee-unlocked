"""Regression coverage for the bundled Trinitron preset's low-resolution Auto fit."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SHADER = ROOT / "port/third_party/slang-shaders/crt/shaders/crt-yo6/crt-yo6-flat-trinitron-tv.slang"
TEXT = SHADER.read_text(encoding="utf-8")

PATTERNS = ((2, 3), (3, 4), (3, 5), (4, 6), (5, 7), (5, 8), (6, 9),
            (7, 10), (7, 11), (8, 12), (9, 13), (9, 14), (10, 15))


def auto_geometry(width: int, height: int, source_height: int = 480):
    vsize = float(source_height)
    tvl = 2.0 * vsize
    crt_size = (tvl, vsize)
    zoom = 0
    for level in range(1, len(PATTERNS)):
        candidate = (PATTERNS[level][0] * crt_size[0], PATTERNS[level][1] * crt_size[1])
        if candidate[0] > width or candidate[1] > height:
            break
        zoom = level
    out_size = (PATTERNS[zoom][0] * crt_size[0], PATTERNS[zoom][1] * crt_size[1])
    scale = (width / out_size[0], height / out_size[1])
    fitted = (out_size[0] * scale[0], out_size[1] * scale[1])
    pitch = (PATTERNS[zoom][0] * scale[0], PATTERNS[zoom][1] * scale[1])
    # Last output pixel center mapped back to a source row/column by the shader's pitch math.
    last_column = int((fitted[0] - 0.5) / pitch[0])
    last_row = int((fitted[1] - 0.5) / pitch[1])
    return fitted, (last_column, last_row), zoom


def main():
    assert "if (int(params.VZOOM) <= 2)" in TEXT, "fit must apply to Auto, not manual zoom"
    assert "raster_scale = params.OutputSize.xy / out_sz;" in TEXT
    assert "vec2 raster_pitch = pat_nfo_a[zoom].xy * raster_scale;" in TEXT
    assert "vec2 fitted_sz = out_sz * raster_scale;" in TEXT
    assert "TexCoord * params.OutputSize.xy) - floor((params.OutputSize.xy - fitted_sz) / 2.0)" in TEXT

    # The 1080p screenshot report used a 4:3 viewport. The 16:9 case must also fill its viewport
    # rather than leaving new pillar/letterbox bars after the crop is removed.
    for output in ((1440, 1080), (1920, 1080), (1280, 720)):
        (fit_w, fit_h), (last_col, last_row), _ = auto_geometry(*output)
        assert abs(fit_w - output[0]) < 1e-6, (output, fit_w)
        assert abs(fit_h - output[1]) < 1e-6, (output, fit_h)
        assert last_col == 959, (output, last_col)
        assert last_row == 479, (output, last_row)

    # Above the smallest pattern the picture still fills the viewport at its aspect (4:3 or 16:9),
    # rather than staying at the pattern's own aspect inside it.
    for output in ((2560, 1440), (3840, 2160), (2880, 2160), (1600, 900)):
        (fit_w, fit_h), (last_col, last_row), _ = auto_geometry(*output)
        assert abs(fit_w - output[0]) < 1e-6 and abs(fit_h - output[1]) < 1e-6, (output, fit_w, fit_h)
        assert (last_col, last_row) == (959, 479), (output, last_col, last_row)

if __name__ == "__main__":
    main()
    print("Trinitron Auto fit: full source visible at 720p/1080p and the viewport aspect kept at every size")
