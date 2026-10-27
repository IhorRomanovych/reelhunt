// frame.glsl - descriptor set 0 of the scene pipelines. Mirrors gpu_types.h (FrameUniforms,
// GpuLight); include with GL_GOOGLE_include_directive.

layout(set = 0, binding = 0) uniform Frame {
    mat4 view;
    mat4 viewProj;
    mat4 invViewProj;
    vec4 cameraPos;      // xyz
    vec4 viewport;       // xy = target size, z = light count, w = lighting mode
    vec4 sunDirection;   // xyz = direction the light travels, w = intensity
    vec4 sunColor;       // rgb, w = 1 when sun shadows are on
    vec4 ambient;        // rgb = default ambient, w = baked-light scale
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;  // view distance where each cascade ends
    vec4 cascadeTexel;   // world size of one shadow texel per cascade
    vec4 shadowParams;   // x = 1 / map size, y = cascade count
    vec4 lampParams;     // x = 1 / lamp shadow map size, y = lamp scale, z = baked shadow floor, w = debug view
    vec4 sky;            // rgb = sky (background) and fog colour
    vec4 fog;            // x = fog start, y = fog end (view depth), z = 1 when fog is on
} frame;

struct Light {
    vec4 positionRadius; // xyz, range
    vec4 colorIntensity; // rgb, intensity
    vec4 directionCos;   // xyz spot direction, w cos(outer); point: w = -2
    vec4 params;         // x cos(inner), y shadow slot (< 0: none), z RW flags (1 atomics, 2 world; 0 a lamp)
    mat4 shadowViewProj;
};
layout(std430, set = 0, binding = 2) readonly buffer Lights {
    Light lights[];
};

const int kLightingBaked = 0;
const int kLightingMidday = 1;
const int kLightingMixed = 2;
const int kLightingDynamic = 3;

// The level's linear fog: fades a lit colour into the sky colour by view depth.
vec3 applyFog(vec3 color, float viewDepth) {
    if (frame.fog.z < 0.5)
        return color;
    float f = clamp((viewDepth - frame.fog.x) / max(frame.fog.y - frame.fog.x, 1e-3), 0.0, 1.0);
    return mix(color, frame.sky.rgb, f);
}
