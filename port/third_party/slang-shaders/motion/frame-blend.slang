#version 450
// Melee Unlocked: blends each presented frame with the one before it, a soft trail on fast motion
// like the frame pacing of older versions. The weight is the share of the previous frame.

layout(push_constant) uniform Push
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
    float BLEND;
} params;

#pragma parameter BLEND "Previous frame weight" 0.35 0.0 0.8 0.05

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
} global;

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
layout(set = 0, binding = 3) uniform sampler2D OriginalHistory1;

void main()
{
    vec3 now = texture(Source, vTexCoord).rgb;
    vec3 before = texture(OriginalHistory1, vTexCoord).rgb;
    FragColor = vec4(mix(now, before, params.BLEND), 1.0);
}
