#version 450
// Shadow depth pass: cut-out casters (chain-link fences, foliage) cast through their holes.
#extension GL_GOOGLE_include_directive : require
#include "draw.glsl"

layout(location = 0) in vec2 vUv;
layout(set = 1, binding = 0) uniform sampler2D albedoTex;

void main() {
    if (draw.material.z > 0.5 && texture(albedoTex, vUv).a < 0.5)
        discard;
}
