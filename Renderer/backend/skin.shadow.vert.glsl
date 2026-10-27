#version 450
// Shadow depth pass, skinned meshes.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "draw.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUv;
layout(location = 4) in uvec4 aBoneIndex;
layout(location = 5) in vec4 aBoneWeight;
layout(location = 0) out vec2 vUv;

void main() {
    vUv = aUv;
    gl_Position = frame.cascadeViewProj[draw.cascade] * (skinMatrix(aBoneIndex, aBoneWeight) * vec4(aPos, 1.0));
}
