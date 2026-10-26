#version 450
// Lola Retro - the encoder of the super lite presets (see README). The pass itself is in
// encode.inc. The encoder's chroma filter and luma trap read each source pixel once: the chroma
// filter is the Gaussian integrated over the pixel exactly, the trap takes the pixel's centre.
// It also multiplies its signal by the TV decoder's reference subcarrier (products), so that
// decode-super-lite.slang only has to low-pass it.

#define LR_SUPER_LITE
#include "encode.inc"
