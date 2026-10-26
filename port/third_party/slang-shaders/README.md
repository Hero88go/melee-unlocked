# CRT shader presets

The `crt` presets of the libretro slang shader collection (https://github.com/libretro/slang-shaders)
with the shader, include and picture files they use from the rest of the collection. A release
carries this folder as one file, `Shaders\crt-shaders.pak`, and the game writes it out to
`Shaders\slang-shaders` on first use, so the presets can be picked under Video without a separate
download.

Nothing here is compiled into the program. The files are unchanged apart from three dash characters
in comments of two shaders, written as hyphens. Each file carries the
copyright and licence notice of its author at the top (GPL, MIT, public domain and similar terms).

`lola-retro/` is Lola Retro (https://github.com/zoran123456/lola-retro, MIT, see its LICENSE): a CRT
model that runs the picture through the console's video encoder, the cable, the TV's decoder, the
beam, the mask and the glass. Its files are unchanged apart from dash characters, written as hyphens. `lola-retro/melee/` holds three presets
made from its GameCube preset (console timing 5: 13.5 MHz, 640 wide; flat screen) for a TV on
composite, a TV on S-Video and an RGB monitor.
