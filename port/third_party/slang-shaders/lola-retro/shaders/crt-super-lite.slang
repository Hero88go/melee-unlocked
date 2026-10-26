#version 450
// Lola Retro - the CRT pass of the super lite presets (see README). The pass itself is in
// crt.inc, as in crt-lite.slang (it writes the screen, no afterglow, nothing around the tube),
// with 3 beam lines per pixel instead of 4 and a cheaper dither.

#define LR_NO_FEEDBACK
#define LR_NO_BACKDROP
#define LR_PRESENT
#define LR_SUPER_LITE
#include "crt.inc"
