#version 450
// Fullscreen-triangle vertex stage for the deferred lighting pass. No vertex inputs:
// the three corners are generated from gl_VertexIndex, so no vertex buffer is bound.
layout(location = 0) out vec2 fragTexCoord;

void main() {
    vec2 p[3];
    p[0] = vec2(-1.0, -1.0);
    p[1] = vec2(3.0, -1.0);
    p[2] = vec2(-1.0, 3.0);
    gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord = p[gl_VertexIndex] * 0.5 + 0.5; // -> [0,1] across the screen
}
