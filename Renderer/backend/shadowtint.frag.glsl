#version 450
// Sun transmittance pass: a blended caster (glass, soft mesh) seen from the sun. Its colour is
// multiplied into the cascade's tint (Blend::Transmit), as much as it is opaque: white where
// clear, the pane's colour where solid. Alpha keeps the nearest pane's depth (MIN blend), so only
// what lies behind a pane is tinted.
#extension GL_GOOGLE_include_directive : require
#include "draw.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outTint;
layout(set = 1, binding = 0) uniform sampler2D albedoTex;

void main() {
    vec4 t = texture(albedoTex, vUv) * vec4(draw.tint.rgb, 1.0);
    outTint = vec4(mix(vec3(1.0), t.rgb, t.a), gl_FragCoord.z);
}
