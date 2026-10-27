#version 450
// Lamp shadow maps, rigid meshes: world position through light draw.cascade's shadow camera.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "draw.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUv;
layout(location = 0) out vec2 vUv;

void main() {
    vUv = aUv;
    gl_Position = lights[draw.cascade].shadowViewProj * (draw.model * vec4(aPos, 1.0));
}
