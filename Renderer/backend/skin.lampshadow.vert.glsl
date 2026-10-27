#version 450
// Lamp shadow maps, skinned meshes (characters).
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
    gl_Position = lights[draw.cascade].shadowViewProj * (skinMatrix(aBoneIndex, aBoneWeight) * vec4(aPos, 1.0));
}
