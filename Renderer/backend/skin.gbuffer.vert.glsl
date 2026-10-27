#version 450
// G-buffer vertex stage, skinned meshes: blends up to four bones of the mesh's pose.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "draw.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
layout(location = 4) in uvec4 aBoneIndex;
layout(location = 5) in vec4 aBoneWeight;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vColor;
layout(location = 3) out vec3 vWorld; // used by the forward translucent pass

void main() {
    mat4 skin = skinMatrix(aBoneIndex, aBoneWeight);
    vec4 world = skin * vec4(aPos, 1.0);
    vNormal = mat3(skin) * aNormal;
    vUv = aUv;
    vColor = aColor;
    vWorld = world.xyz;
    gl_Position = frame.viewProj * world;
}
