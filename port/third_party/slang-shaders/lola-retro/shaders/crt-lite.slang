#version 450
// Lola Retro - pass 6 of the lite presets, for weak GPUs (older phones, see README). The pass
// itself is in crt.inc. It writes the screen itself, so the present pass and the full-screen
// float target between the two are not needed, and leaves out the phosphor afterglow (it reads a
// second full-screen float target back every frame) and Around the tube.

#define LR_NO_FEEDBACK
#define LR_NO_BACKDROP
#define LR_PRESENT
#include "crt.inc"
