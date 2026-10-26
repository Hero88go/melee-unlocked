#version 450
// Lola Retro - pass 7 for HDR displays: linear light -> HDR10 (Rec.2020 primaries, PQ curve).
// A 10-bit last pass tells RetroArch that the shader writes HDR10 itself, so RetroArch passes
// it straight to the display. HDR must be on in RetroArch, with the Vulkan, D3D11 or D3D12 driver.

#pragma format A2B10G10R10_UNORM_PACK32

#define LR_HDR
#include "hdr.inc"
#include "common.inc"

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

// Rec.709 to Rec.2020 primaries, both D65 (ITU-R BT.2087). Columns are the source channels.
const mat3 k709to2020 = mat3(
    0.6274040, 0.0690970, 0.0163916,
    0.3292820, 0.9195400, 0.0880132,
    0.0433136, 0.0113612, 0.8955950);

// SMPTE ST 2084 (PQ): absolute luminance in nits -> signal 0..1
vec3 pq(vec3 nits)
{
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

void main()
{
    vec3 c = textureLod(Source, vTexCoord, 0.0).rgb;   // linear Rec.709, 1.0 = paper white;
                                                      // phosphor colours outside Rec.709 stay in Rec.2020
    vec3 nits = max(k709to2020 * c, 0.0) * global.LR_NITS_WHITE;
    vec3 e = pq(nits);
    vec2 fc = vTexCoord * params.OutputSize.xy;
    e += (hash13(vec3(fc, 1.0)) - 0.5) / 1023.0;                 // dither for the 10-bit output
    FragColor = vec4(clamp(e, 0.0, 1.0), 1.0);
}
