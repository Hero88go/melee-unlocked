# UI source attribution and removal map

This file records which third-party projects contribute to the in-game UI and
where their code or assets live. Keep imported files inside the named source
boundary so a later removal does not require searching shared settings code.

## GD's Melee UI

The settings UI's third appearance option ports the current settings toolkit
from [GD's Melee](https://github.com/GurekamDhillon/gd-melee-workspace).
The previous adaptation reference was commit
`9b6753cd296955b065df7b3f7bbfbe08178eeb01`. Source reviews were made on
2026-09-23 at `GurekamDhillon/gd-melee-workspace`
(`477aff14eb2608ccf7780f936e65136cb1c1cbe2`) and its runtime repository,
`GurekamDhillon/melee` branch `pc-port`
(`efc484806c4137cca67bbc9129a29e1d863a54d7`). Relevant files include
`menu/pipeline/hub_layout.py`, `menu/pipeline/hub_motion.py`,
`menu/out_hub/{hub_layout,hub_motion}.json`, and `src/melee/gm/gmfrontend*`.

The upstream project is licensed under GNU GPL version 2 or later. Its native
frontend runs inside a separate compiled-decompilation engine and is not a
drop-in module for this checkout's static-recompilation runtime. This project
is porting its layout and motion contracts into its own renderer. The slide
curves are integrated in `port/runtime/gx/ui_sources/gd_melee/motion.h`, and
the extracted source art is in that directory's `assets/`. The settings
controls still live in `port/runtime/gx/pc_settings.cpp`, so this is
not yet an exact port of the full frontend. Keep further ported files and
assets under `port/runtime/gx/ui_sources/gd_melee/` with their source revision
and notices, and call them through a small adapter. See
`port/runtime/gx/ui_sources/README.md` for the removal boundary.

The current toolkit import is in `assets/kit/`: the Options palette, chrome,
list/widget geometry, source button glyphs, and eight Source Sans 3 font atlases
with their exact glyph metrics. `text.inl` draws the atlas glyphs with the
original kerning and shear. Source Sans 3 has its own SIL OFL notice in
`assets/kit/SourceSans3-OFL.md`. `tools/import_gd_kit.py` makes this import
reproducible from the reviewed revisions. Old blue frame textures are retained
for attribution/history but no longer define the GD settings layout.

## YAMPP

The [YAMPP source](https://github.com/sonsegajp/YAMPP) was reviewed at commit
`b560eb9e64bcf07dbe5d4251483d609317fc60a3`. Its native-menu work was inspected
as a reference, but no YAMPP UI code, assets, screenshots, fonts, or game
resources have been copied into the shipping source or test package as of this
revision. The local `build-integration/yampp-source-review` checkout is
temporary research material and must not be packaged.

If YAMPP-derived UI content is integrated, put every such file under
`port/runtime/gx/ui_sources/yampp/`, include the source revision and a per-file
origin/license manifest there, and include that directory's notices in the
test package. Do not mix those files into `pc_settings.cpp` or the GD Melee
directory. Deleting `port/runtime/gx/ui_sources/yampp/` and its single adapter
will remove those additions without affecting the other appearance options.

YAMPP's source credits list licenses for components it uses, but the review
checkout did not provide a top-level YAMPP license grant. Do not copy a YAMPP
file into this project until its applicable file-level license is confirmed.
