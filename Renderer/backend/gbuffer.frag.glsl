#version 450
// G-buffer fragment stage (rigid and skinned): albedo, normal, material and light targets.
#extension GL_GOOGLE_include_directive : require
#include "draw.glsl"

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vColor;

layout(location = 0) out vec4 oAlbedo;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oMaterial;
layout(location = 3) out vec4 oLight;

layout(set = 1, binding = 0) uniform sampler2D albedoTex;

void main() {
    vec4 tex = texture(albedoTex, vUv);
    if (draw.material.z > 0.5 && tex.a < 0.5)
        discard; // cut-out texture (fences, foliage)
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing)
        N = -N; // double-sided geometry: light the side we see
    oAlbedo = vec4(tex.rgb * draw.tint.rgb, 1.0);
    oNormal = vec4(N * 0.5 + 0.5, 1.0);
    oMaterial = vec4(draw.material.x, draw.material.y, draw.material.w, 1.0);
    // Baked meshes carry their light in the vertex colour; the others their ambient override.
    oLight = draw.material.w > 0.5 ? vec4(vColor.rgb, 1.0) : vec4(draw.ambient.rgb, draw.ambient.w > 0.0 ? 1.0 : 0.0);
}
