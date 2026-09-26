# GD Melee UI import boundary

**Status: settings toolkit port in progress; visual and interaction validation required.**

Reference repositories:

- Art and motion workspace: <https://github.com/GurekamDhillon/gd-melee-workspace>
- Runtime frontend: <https://github.com/GurekamDhillon/melee/tree/pc-port>

Reviewed revisions: workspace
`477aff14eb2608ccf7780f936e65136cb1c1cbe2`; runtime
`efc484806c4137cca67bbc9129a29e1d863a54d7`.

The menu motion source is `menu/pipeline/hub_motion.py`. `motion.h` ports its
`ev_slide_in` and `ev_slide_out` keyframes. Runtime menu structure and behavior
are specified by `src/melee/gm/gmfrontend.c` and its included frontend files;
they run inside a separate compiled-decompilation engine and are not drop-in
source for this static-recompilation renderer. Both repositories state
GPL-2.0-or-later for their source.

Files in `assets/` are extracted `mergedimage.png` previews from these workspace
sources at the revision above: `menu/out/ora/frame_*.ora`, `panel_bg.ora`,
`row_ng.ora`, `row_sel.ora`, `glyph_a.ora`, `glyph_b.ora`, `cursor_hand.ora`,
`btn_play_*.ora`, and `btn_continue_*.ora`. They are drawn by the GD appearance
option and packaged under `ui_sources/gd_melee/`. The workspace calls this art
placeholder art; it can change upstream. Attribution and GPL notice are retained.

Keep every future GD Melee-derived menu file, image, font, layout, or animation
in this directory and record its original path and license before building it.
Do not copy game-derived Nintendo assets or screenshots here. Route calls
through a small adapter so deleting this directory and its build entry removes
this UI treatment without touching the other appearances.

## Current toolkit import (2026-09-24)

`assets/kit/` contains the current workspace's `menu/out_kit` palette, chrome,
list, widget, and motion JSON. This toolkit supersedes the older blue frame art:
the Options section uses gray faces, bone text, gold selection, and a uniform
0.25 shear. The runtime reference is `gmfrontend_kit.inc` and
`gmfrontend_kitlist.inc`, not the legacy `fe_draw_frame` fallback.

The eight Source Sans 3 font atlases are decoded losslessly from the reviewed
workspace's `_build/ui/font_*_latin_0.gxtex` using the runtime repository's
`pc/tools/png2gx.py`. `tools/import_gd_kit.py` preserves the original manifest
and generates `font_metrics.h` from its glyph advances, offsets, UVs, and
kerning. `text.inl` adapts these metrics and atlases to our renderer, including
the source's per-vertex shear. The source OTFs come from
`menu/SourceSans3/`; their SIL Open Font License is included as
`assets/kit/SourceSans3-OFL.md`. These assets are not Nintendo font assets.

Regenerate using the two reviewed checkouts:

```
python tools/import_gd_kit.py <gd-melee-workspace> <gd-melee-runtime>
```
