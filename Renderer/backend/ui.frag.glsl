#version 450
// 2D overlay: texel * vertex colour (solid quads sample the white default texture).
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D tex;
// The brightness setting's gamma, as on the scene (composite.frag.glsl).
layout(push_constant) uniform Brightness { float gamma; } pc;

void main() {
    vec4 c = texture(tex, vUv) * vColor;
    outColor = vec4(pow(clamp(c.rgb, 0.0, 1.0), vec3(pc.gamma)), c.a);
}
