#version 450
// 2D overlay (menus, HUD). Positions are normalised window coordinates: (0,0) top-left.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor; // R8G8B8A8_UNORM

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
    vUv = aUv;
    vColor = aColor;
    gl_Position = vec4(aPos * 2.0 - 1.0, 0.0, 1.0); // Vulkan NDC: y = -1 is the top
}
