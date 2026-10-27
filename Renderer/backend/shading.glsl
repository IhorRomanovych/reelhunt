// shading.glsl - how a surface is lit, shared by the deferred lighting pass (light.frag) and
// the forward translucent pass (forward.frag). The includer declares, before including:
//   uniform sampler2DArrayShadow shadowMap;      sun/moon cascades
//   uniform sampler2DArrayShadow lampShadowMap;  two layers per shadowed lamp: 2s static, 2s+1 moving
//   uniform sampler2DArray shadowTint;           the cascades' transmittance (rgb, nearest pane depth in a)
//   uniform sampler2DArray lampTint;             the same for each shadowed lamp's slot
// and includes frame.glsl first.
//
// Modes (frame.viewport.w):
//   Baked   - the original: baked surfaces show texture x prelight; the rest their ambient.
//   Midday  - debug sun with cascaded shadows, lamps, flat ambient; prelight ignored.
//   Mixed   - Baked plus the lamps as real lights: they light characters (shadowed by the
//             static scene and by other characters) and, on baked surfaces, a moving caster's
//             shadow removes the lamp's share of the prelight (static occluders are already in it).
//   Dynamic - prelight ignored: moon (cascaded shadows) + shadowed lamps + a dim ambient.

const float PI = 3.14159265359;

struct Surface {
    vec3 P;          // world position
    vec3 N;          // unit normal, facing the viewer
    vec3 V;          // unit vector to the eye
    vec3 albedo;
    float metallic;
    float roughness;
    bool baked;      // carries the level's prelight
    vec3 prelight;   // baked light (baked surfaces)
    vec3 ambient;    // ambient for dynamically lit surfaces
    float viewDepth; // distance along the view direction
};

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a2 = roughness * roughness * roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Diffuse + specular from one light of `radiance` arriving along L.
vec3 brdf(Surface s, vec3 L, vec3 radiance) {
    float NdotL = max(dot(s.N, L), 0.0);
    if (NdotL <= 0.0)
        return vec3(0.0);
    float NdotV = max(dot(s.N, s.V), 1e-4);
    vec3 H = normalize(s.V + L);
    vec3 F0 = mix(vec3(0.04), s.albedo, s.metallic);
    vec3 F = fresnelSchlick(max(dot(H, s.V), 0.0), F0);
    vec3 specular = distributionGGX(s.N, H, s.roughness) * geometrySmith(NdotV, NdotL, s.roughness) * F /
                    (4.0 * NdotV * NdotL + 1e-4);
    vec3 kD = (1.0 - F) * (1.0 - s.metallic);
    return (kD * s.albedo / PI + specular) * radiance * NdotL;
}

// Fraction of sun/moon light reaching P (1 = lit): cascade by view depth, a texel of normal
// offset, 3x3 hardware-compared taps (each a 2x2 bilinear PCF), faded out at the far end.
float sunShadow(Surface s) {
    int count = int(frame.shadowParams.y);
    if (count == 0 || s.viewDepth > frame.cascadeSplits[count - 1])
        return 1.0;
    int c = 0;
    while (c < count - 1 && s.viewDepth > frame.cascadeSplits[c])
        ++c;
    vec4 sc = frame.cascadeViewProj[c] * vec4(s.P + s.N * frame.cascadeTexel[c] * 1.5, 1.0);
    vec3 p = sc.xyz / sc.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    float texel = frame.shadowParams.x;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(shadowMap, vec4(uv + vec2(x, y) * texel, float(c), p.z));
    lit /= 9.0;
    float far = frame.cascadeSplits[count - 1];
    return mix(lit, 1.0, smoothstep(far * 0.9, far, s.viewDepth));
}

// The colour sun/moon light keeps on its way to P: the panes' (glass, soft mesh) between it and
// P multiplied, white where none is (a pane at P itself does not tint it).
vec3 sunTint(Surface s) {
    int count = int(frame.shadowParams.y);
    if (count == 0 || s.viewDepth > frame.cascadeSplits[count - 1])
        return vec3(1.0);
    int c = 0;
    while (c < count - 1 && s.viewDepth > frame.cascadeSplits[c])
        ++c;
    vec4 sc = frame.cascadeViewProj[c] * vec4(s.P + s.N * frame.cascadeTexel[c] * 1.5, 1.0);
    vec3 p = sc.xyz / sc.w;
    vec4 t = texture(shadowTint, vec3(p.xy * 0.5 + 0.5, float(c)));
    return p.z > t.a + 0.002 ? t.rgb : vec3(1.0);
}

// Light from lamp i arriving at P (no shadow): windowed inverse-square falloff over the light's
// range, spot cone, colour x intensity x the scene's lamp scale. L = unit vector to the light.
vec3 lampRadiance(int i, vec3 P, out vec3 L) {
    vec3 toLight = lights[i].positionRadius.xyz - P;
    float d = length(toLight);
    float range = max(lights[i].positionRadius.w, 1e-3);
    L = toLight / max(d, 1e-4);
    if (d >= range)
        return vec3(0.0);
    float x = d / range;
    float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
    float atten = window * window / (d * d + 1.0);
    float spot = 1.0;
    if (lights[i].directionCos.w > -1.5)
        spot = smoothstep(lights[i].directionCos.w, lights[i].params.x, dot(-L, normalize(lights[i].directionCos.xyz)));
    if (lights[i].params.z > 0.5) // a RenderWare light: linear to its radius (ours), no lamp scale
        return lights[i].colorIntensity.rgb * (lights[i].colorIntensity.a * (1.0 - x) * spot);
    return lights[i].colorIntensity.rgb * (lights[i].colorIntensity.a * frame.lampParams.y * atten * spot);
}

float lampShadow(int i, int which, vec3 P, vec3 N);

// The level's RenderWare lights (Light::rwFlags) on a surface, as RenderWare adds a light to the
// prelight: colour x N.L; `flag` 1 the atomics' lights, 2 the world's. One with a shadow slot (a
// torch, whose lit cone the world's surfaces clip) is held back by the static scene.
vec3 rwLights(Surface s, int flag) {
    vec3 sum = vec3(0.0);
    int count = int(frame.viewport.z);
    for (int i = 0; i < count; ++i) {
        if ((int(lights[i].params.z) & flag) == 0)
            continue;
        vec3 L;
        vec3 radiance = lampRadiance(i, s.P, L);
        float NdotL = max(dot(s.N, L), 0.0);
        if (NdotL <= 0.0 || dot(radiance, vec3(1.0)) <= 0.0)
            continue;
        sum += s.albedo * radiance * NdotL * lampShadow(i, 0, s.P, s.N);
    }
    return sum;
}

// Visibility of lamp i at P in one of its shadow layers (which: 0 static scene, 1 moving
// casters). Outside the map = lit. A few centimetres of normal offset against acne.
float lampShadow(int i, int which, vec3 P, vec3 N) {
    int slot = int(lights[i].params.y);
    if (slot < 0)
        return 1.0;
    vec4 sc = lights[i].shadowViewProj * vec4(P + N * 0.04, 1.0);
    if (sc.w <= 0.0)
        return 1.0;
    vec3 p = sc.xyz / sc.w;
    if (any(greaterThan(abs(p.xy), vec2(1.0))) || p.z > 1.0)
        return 1.0;
    vec2 uv = p.xy * 0.5 + 0.5;
    float texel = frame.lampParams.x;
    float layer = float(slot * 2 + which);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(lampShadowMap, vec4(uv + vec2(x, y) * texel, layer, p.z));
    return lit / 9.0;
}

// The colour lamp i's light keeps on its way to P through the panes (as sunTint).
vec3 lampTintAt(int i, vec3 P, vec3 N) {
    int slot = int(lights[i].params.y);
    if (slot < 0)
        return vec3(1.0);
    vec4 sc = lights[i].shadowViewProj * vec4(P + N * 0.04, 1.0);
    if (sc.w <= 0.0)
        return vec3(1.0);
    vec3 p = sc.xyz / sc.w;
    if (any(greaterThan(abs(p.xy), vec2(1.0))) || p.z > 1.0)
        return vec3(1.0);
    vec4 t = texture(lampTint, vec3(p.xy * 0.5 + 0.5, float(slot)));
    return p.z > t.a + 0.0005 ? t.rgb : vec3(1.0);
}

// Every lamp, fully shadowed (static scene and moving casters), tinted by the panes.
vec3 lamps(Surface s) {
    vec3 sum = vec3(0.0);
    int count = int(frame.viewport.z);
    for (int i = 0; i < count; ++i) {
        if (lights[i].params.z > 0.5)
            continue; // (rwLights)
        vec3 L;
        vec3 radiance = lampRadiance(i, s.P, L);
        if (dot(radiance, vec3(1.0)) <= 0.0 || dot(s.N, L) <= 0.0)
            continue;
        float vis = lampShadow(i, 0, s.P, s.N) * lampShadow(i, 1, s.P, s.N);
        sum += brdf(s, L, radiance) * vis * lampTintAt(i, s.P, s.N);
    }
    return sum;
}

// The share of a baked surface's light that moving casters (characters) take away from the
// lamps: where the static scene lets the lamp through but a character blocks it.
vec3 lampLightBlocked(Surface s) {
    vec3 sum = vec3(0.0);
    int count = int(frame.viewport.z);
    for (int i = 0; i < count; ++i) {
        if (int(lights[i].params.y) < 0 || lights[i].params.z > 0.5)
            continue;
        vec3 L;
        vec3 radiance = lampRadiance(i, s.P, L);
        float NdotL = dot(s.N, L);
        if (dot(radiance, vec3(1.0)) <= 0.0 || NdotL <= 0.0)
            continue;
        float blocked = lampShadow(i, 0, s.P, s.N) * (1.0 - lampShadow(i, 1, s.P, s.N));
        sum += s.albedo / PI * radiance * NdotL * blocked;
    }
    return sum;
}

vec3 shade(Surface s) {
    int mode = int(frame.viewport.w);
    vec3 baked = s.albedo * s.prelight * frame.ambient.w;
    // The RenderWare lights: the world's on baked surfaces (ours: prelit props count as world),
    // the atomics' on the rest.
    vec3 rw = rwLights(s, s.baked ? 2 : 1);
    if (mode == kLightingBaked)
        return (s.baked ? baked : s.albedo * s.ambient) + rw;
    if (mode == kLightingMixed) {
        if (s.baked)
            return max(baked - lampLightBlocked(s), baked * frame.lampParams.z) + rw;
        return s.albedo * s.ambient + lamps(s) + rw;
    }
    // Midday (sun) and Dynamic (moon): direct light from the sky with cascaded shadows, the
    // lamps, and the frame's ambient; the prelight is not used.
    vec3 L = normalize(-frame.sunDirection.xyz);
    vec3 shadow = frame.sunColor.w > 0.5 ? sunShadow(s) * sunTint(s) : vec3(1.0);
    vec3 color = brdf(s, L, frame.sunColor.rgb * frame.sunDirection.w) * shadow + lamps(s);
    vec3 F = fresnelSchlick(max(dot(s.N, s.V), 0.0), mix(vec3(0.04), s.albedo, s.metallic));
    return color + rw + (1.0 - F) * (1.0 - s.metallic) * s.albedo * frame.ambient.rgb;
}
