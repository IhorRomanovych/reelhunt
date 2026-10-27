#version 450
// Final composite pass. Samples the lit scene (HDR; clamped by the UNORM swapchain),
// applies screen effects (camcorder look, vignette, flash, fade), and optionally blends the
// Bink video overlay on top with alpha.
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneSampler;
layout(set = 0, binding = 1) uniform sampler2D videoSampler;

layout(set = 0, binding = 2) uniform EffectState {
    vec4 fadeColor;         // rgb = color, a = opacity (0 = no fade, 1 = fully faded)
    vec4 flashColor;        // rgb = color, a = intensity (decays over time)
    float vignetteStrength; // 0..1 (used during executions)
    float videoActive;      // 0 or 1 (video overlay enabled)
    float noise;            // white noise (video static) 0..1
    float grain;            // film grain 0..1
    float scanlines;        // interlace lines 0..1
    float time;             // seconds
    float height;           // target height in pixels
    float gamma;            // the brightness setting: every colour to this power
} fx;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = fragTexCoord;
    float frame = floor(fx.time * 30.0); // the noise changes 30 times a second, like video
    // Static tears rows sideways a little.
    if (fx.noise > 0.0) {
        float row = floor(uv.y * fx.height / 3.0);
        uv.x += (hash(vec2(row, frame)) - 0.5) * 0.03 * fx.noise;
    }
    vec3 col = texture(sceneSampler, uv).rgb;

    // Camcorder look: grain, interlace lines, white-noise static.
    if (fx.grain > 0.0)
        col += (hash(gl_FragCoord.xy + frame * 17.0) - 0.5) * 0.12 * fx.grain;
    if (fx.scanlines > 0.0)
        col *= 1.0 - fx.scanlines * 0.18 * step(1.0, mod(gl_FragCoord.y, 2.0));
    if (fx.noise > 0.0)
        col = mix(col, vec3(hash(gl_FragCoord.xy * 0.5 + frame * 31.0)), clamp(fx.noise, 0.0, 1.0) * 0.85);

    // Vignette: darken corners based on radial distance from center.
    if (fx.vignetteStrength > 0.0) {
        vec2 d = fragTexCoord - 0.5;
        float dist = length(d) * 1.4142; // normalize so corners = 1.0
        float vig = 1.0 - fx.vignetteStrength * smoothstep(0.4, 1.0, dist);
        col *= vig;
    }

    // Flash: add a colored pulse (damage = red, execution = white).
    col += fx.flashColor.rgb * fx.flashColor.a;

    // Fade: lerp toward fadeColor (level transitions).
    if (fx.fadeColor.a > 0.0)
        col = mix(col, fx.fadeColor.rgb, fx.fadeColor.a);

    // Video overlay: blend Bink frame on top when active.
    if (fx.videoActive > 0.5) {
        vec4 vid = texture(videoSampler, fragTexCoord);
        col = mix(col, vid.rgb, vid.a);
    }

    // Brightness: the original's gamma ramp (exe 0x5db0d0).
    col = pow(clamp(col, 0.0, 1.0), vec3(fx.gamma));
    outColor = vec4(col, 1.0);
}
