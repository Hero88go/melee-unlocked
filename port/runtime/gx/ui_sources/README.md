# Third-party UI boundaries

UI code or assets adapted from another project must stay under a dedicated
directory here. Shared settings behavior belongs in `pc_settings.cpp`; it may
call a source-specific adapter but must not inline third-party art, geometry,
or animation data.

| Directory | Source | Current status | Removal |
|---|---|---|---|
| `gd_melee/` | GD's Melee, GPL-2.0-or-later | `motion.h` contains ported panel slide curves and `assets/` contains extracted source menu art; settings controls remain in `pc_settings.cpp` | Remove the GD appearance adapter calls, its motion state fields, and `gd_melee/`; no CMake entry is used |
| `mockups/` | User-selected mockup sheets | `assets/` contains reference thumbnails for appearance selection | Remove its asset directory and the appearance preview references |
| `yampp/` | YAMPP | No YAMPP files are in the product source yet; this directory is a labeled import boundary | Remove `yampp/` and any future YAMPP adapter/CMake entry |

The source review clones under `build-integration/` are not product source,
must not be committed or packaged, and may be deleted independently. Keep an
`ORIGIN.md` beside any imported files with repository, commit, license, and
file-by-file origin. Preserve upstream copyright and license notices.
