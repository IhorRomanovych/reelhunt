#version 450
// G-buffer vertex stage, rigid meshes: model -> world by the draw's transform.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "draw.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor; // baked light (prelight)

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vColor;
layout(location = 3) out vec3 vWorld; // used by the forward translucent pass

void main() {
    vec4 world = draw.model * vec4(aPos, 1.0);
    vNormal = transpose(inverse(mat3(draw.model))) * aNormal;
    vUv = aUv;
    vColor = aColor;
    vWorld = world.xyz;
    gl_Position = frame.viewProj * world;
}
