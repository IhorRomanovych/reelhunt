#version 450
// Deferred lighting: rebuilds each pixel's surface from the G-buffer and lights it with
// shading.glsl (the modes are described there).
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D gAlbedo;
layout(set = 1, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 2) uniform sampler2D gMaterial;
layout(set = 1, binding = 3) uniform sampler2D gLight;
layout(set = 1, binding = 4) uniform sampler2D gDepth;
layout(set = 1, binding = 5) uniform sampler2DArrayShadow shadowMap;
layout(set = 1, binding = 6) uniform sampler2DArrayShadow lampShadowMap;
layout(set = 1, binding = 7) uniform sampler2DArray shadowTint;
layout(set = 1, binding = 8) uniform sampler2DArray lampTint;

#include "shading.glsl"

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(gDepth, px, 0).r;
    int mode = int(frame.viewport.w);
    if (depth >= 1.0) {
        outColor = vec4(mode == kLightingMidday ? vec3(0.05, 0.06, 0.10) : frame.sky.rgb, 1.0);
        return;
    }

    vec4 world = frame.invViewProj * vec4(vUv * 2.0 - 1.0, depth, 1.0);
    Surface s;
    s.P = world.xyz / world.w;
    s.albedo = texelFetch(gAlbedo, px, 0).rgb;
    vec3 n = texelFetch(gNormal, px, 0).rgb * 2.0 - 1.0;
    s.N = dot(n, n) > 1e-6 ? normalize(n) : vec3(0.0, 1.0, 0.0); // no NaN from a zero normal
    vec4 material = texelFetch(gMaterial, px, 0);
    vec4 light = texelFetch(gLight, px, 0);
    s.metallic = material.r;
    s.roughness = clamp(material.g, 0.05, 1.0);
    s.baked = material.b > 0.5;
    s.V = normalize(frame.cameraPos.xyz - s.P);
    if (dot(s.N, s.V) < 0.0)
        s.N = -s.N;
    s.prelight = light.rgb;
    s.ambient = light.a > 0.5 ? light.rgb : frame.ambient.rgb;
    s.viewDepth = -(frame.view * vec4(s.P, 1.0)).z;
    int debugView = int(frame.lampParams.w);
    if (debugView == 1)
        outColor = vec4(s.albedo, 1.0);
    else if (debugView == 2)
        outColor = vec4(light.rgb, 1.0);
    else if (debugView == 3)
        outColor = vec4(s.N * 0.5 + 0.5, 1.0);
    else if (debugView == 5)
        outColor = vec4(s.albedo * light.rgb, 1.0);
    else if (debugView == 4)
        outColor = vec4(s.baked ? 1.0 : 0.0, light.a, 0.0, 1.0);
    else
        outColor = vec4(applyFog(shade(s), s.viewDepth), 1.0);
}
