#version 450
// Forward translucent pass (glass, decals, soft mesh): the same lighting as the deferred pass
// (shading.glsl), blended over the lit scene with the texture's alpha. Depth-tested against the
// opaque scene, no depth writes.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "draw.glsl"

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vColor;
layout(location = 3) in vec3 vWorld;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D albedoTex;
layout(set = 2, binding = 0) uniform sampler2DArrayShadow shadowMap;
layout(set = 2, binding = 1) uniform sampler2DArrayShadow lampShadowMap;
layout(set = 2, binding = 2) uniform sampler2DArray shadowTint;
layout(set = 2, binding = 3) uniform sampler2DArray lampTint;

#include "shading.glsl"

void main() {
    vec4 tex = texture(albedoTex, vUv);
    if (draw.material.w > 1.5) {
        // Unlit: texture times colour, its alpha the material's (a pickup's halo), no fog.
        outColor = vec4(tex.rgb * draw.tint.rgb, tex.a * draw.tint.a);
        return;
    }
    if (tex.a < 1.0 / 255.0)
        discard;
    Surface s;
    s.P = vWorld;
    s.albedo = tex.rgb * draw.tint.rgb;
    s.N = dot(vNormal, vNormal) > 1e-8 ? normalize(vNormal) : vec3(0.0, 1.0, 0.0);
    s.V = normalize(frame.cameraPos.xyz - s.P);
    if (dot(s.N, s.V) < 0.0)
        s.N = -s.N;
    s.metallic = draw.material.x;
    s.roughness = clamp(draw.material.y, 0.05, 1.0);
    s.baked = draw.material.w > 0.5;
    s.prelight = vColor.rgb;
    s.ambient = draw.ambient.w > 0.0 ? draw.ambient.rgb : frame.ambient.rgb;
    s.viewDepth = -(frame.view * vec4(s.P, 1.0)).z;
    outColor = vec4(applyFog(shade(s), s.viewDepth), tex.a * draw.tint.a); // (a decal's alpha: Game/decals.h)
}
