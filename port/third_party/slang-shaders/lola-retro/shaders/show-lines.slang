#version 450
// Lola Retro - TV show only (the tv-show-broadcast preset, between the beam and the CRT pass):
// one beam row per line of the programme.
// Every line of a 240p/288p console is in both fields of the programme, and a game in the
// other TV standard went through the studio's standards converter, which interpolated between
// console lines (here a Catmull-Rom cubic over four). Doing that here, once per programme line,
// lets the CRT pass read one row per line instead of interpolating at every screen pixel.
// The output is SHOW_LINES_ROWS rows high; that height tells the CRT pass that this pass ran.

#include "common.inc"

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) flat out vec4 vMap;      // showLines(): raster lines per line, scale, offset, programme lines
layout(location = 2) flat out float vLines;   // the console's native lines
layout(location = 3) flat out float vShow;    // 1: a TV show on a TV raster

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
    Timing T = getTiming(params.OriginalSize.xy, params.FrameCount);
    vLines = params.OriginalSize.y / T.scale;
    vMap = showLines(T.pal, T.interlace, vLines);
    vShow = (isShow(T.cable) && T.scan < 0.5) ? 1.0 : 0.0;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) flat in vec4 vMap;
layout(location = 2) flat in float vLines;
layout(location = 3) flat in float vShow;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;   // the beam pass: one row per console line

// Beam of console line g at column x (0 above and below the picture)
vec4 beamRow(int x, float g)
{
    if (g < 0.0 || g >= vLines) return vec4(0.0);
    return texelFetch(Source, ivec2(x, int(g)), 0);
}

void main()
{
    int x = int(floor(vTexCoord.x * params.OutputSize.x));
    float m = floor(vTexCoord.y * params.OutputSize.y);   // the row: a programme line
    if (vShow < 0.5)
    {
        // not a TV show (another cable): the beam pass's rows as they are
        FragColor = beamRow(x, m);
        return;
    }
    if (m >= vMap.w)
    {
        FragColor = vec4(0.0);
        return;
    }
    float p = m * vMap.y + vMap.z;                         // the console line it shows
    float g = floor(p), f = p - g;
    if (f < 1e-3)
    {
        FragColor = beamRow(x, g);
        return;
    }
    vec4 a = beamRow(x, g - 1.0), b = beamRow(x, g), c = beamRow(x, g + 1.0), d = beamRow(x, g + 2.0);
    vec4 r = b + 0.5 * f * (c - a) + f * f * (a - 2.5 * b + 2.0 * c - 0.5 * d)
           + f * f * f * (1.5 * (b - c) + 0.5 * (d - a));
    FragColor = max(r, 0.0);
}
