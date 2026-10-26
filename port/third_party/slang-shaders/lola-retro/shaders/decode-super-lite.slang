#version 450
// Lola Retro - the decoder of the super lite presets (see README). The pass itself is in
// decode.inc. The super lite encoder has already multiplied the signal by this decoder's
// reference subcarrier (encode.inc, products), so the chroma is a plain low-pass of that,
// without demodulating each frame of the subcarrier cycle at every tap; the filters' Gaussian
// weights come by recurrence instead of an exp at every tap. The same picture, to rounding.

#define LR_SUPER_LITE
#include "decode.inc"
