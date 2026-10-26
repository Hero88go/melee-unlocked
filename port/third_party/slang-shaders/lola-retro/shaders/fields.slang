#version 450
// Lola Retro - line doubling or interlace? Used by the GLSL port for Amiberry (two passes,
// before the CRT pass); RetroArch's presets do not need it, PUAE only doubles interlaced screens.
//
// Amiga emulators draw every line of a normal screen twice (720x568 for a 256-line game) and
// weave the two fields of an interlaced screen into the same rows. Only the rows tell them
// apart: a doubled frame has identical row pairs.
// Pass 1 (one texel per row pair): the largest difference inside each pair, for both ways of
// pairing the rows. Pass 2 (one texel): the largest over the whole frame.
// Output: x = rows paired 0-1, 2-3, ...; y = rows paired 1-2, 3-4, ...

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
layout(set = 0, binding = 3) uniform sampler2D Original;

float maxPart(vec3 v) { return max(v.r, max(v.g, v.b)); }

void main()
{
    vec2 d = vec2(0.0);
    if (params.OutputSize.y > 1.5)
    {
        // pass 1: rows 2j, 2j+1 and 2j+1, 2j+2 of the frame
        int w = int(params.OriginalSize.x), h = int(params.OriginalSize.y);
        int r = 2 * int(floor(vTexCoord.y * params.OutputSize.y));
        if (r + 1 < h)
        {
            bool second = r + 2 < h;
            for (int x = 0; x < w; x++)
            {
                vec3 a = texelFetch(Original, ivec2(x, r), 0).rgb;
                vec3 b = texelFetch(Original, ivec2(x, r + 1), 0).rgb;
                d.x = max(d.x, maxPart(abs(a - b)));
                if (second) d.y = max(d.y, maxPart(abs(b - texelFetch(Original, ivec2(x, r + 2), 0).rgb)));
            }
        }
    }
    else
    {
        // pass 2: the largest over all row pairs
        int n = int(params.SourceSize.y);
        for (int j = 0; j < n; j++) d = max(d, texelFetch(Source, ivec2(0, j), 0).rg);
    }
    FragColor = vec4(d, 0.0, 1.0);
}
