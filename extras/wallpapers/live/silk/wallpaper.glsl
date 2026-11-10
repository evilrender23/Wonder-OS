// Silk — ported from React Bits <Silk /> (see ../LICENSE-REACTBITS.md).
// Single pass: the half-res bloom mip chain becomes two rings of samples of
// the same field; pointer drag, click ripples and the intro are dropped.

const float E = 2.71828182845904523536;

float noise(vec2 texCoord) {
    vec2 r = E * sin(E * texCoord);
    return fract(r.x * r.y * (1.0 + texCoord.x));
}

vec2 variation_shift() {
    if (P_VARIATION == 0.0) return vec2(0.0);
    float a = fract(sin(P_VARIATION * 12.9898 + 78.233) * 43758.5453) - 0.5;
    float b = fract(sin(P_VARIATION * 12.9898 + 156.466) * 43758.5453) - 0.5;
    return vec2(a, b) * 6.0;
}

float silk(vec2 vUv, float t) {
    float c = cos(P_ROTATION);
    float s = sin(P_ROTATION);
    vec2 uv = mat2(c, -s, s, c) * (vUv * P_SCALE);
    vec2 tex = uv * P_SCALE + variation_shift();
    tex.y += 0.03 * P_WAVE * sin(8.0 * tex.x - t);
    return 0.6 + 0.4 * sin(5.0 * (tex.x + tex.y + cos(3.0 * tex.x + 5.0 * tex.y) + 0.02 * t)
        + P_RIPPLE * sin(20.0 * (tex.x + tex.y - 0.1 * t)));
}

float bright(vec2 uv, float t) {
    float n = clamp((silk(uv, t) - 0.2) / 0.8, 0.0, 1.0);
    return pow(n, 6.0);
}

vec3 soft_clip(vec3 c) {
    vec3 over = max(c - 0.75, 0.0);
    return min(c, 0.75) + 0.25 * (1.0 - exp(-over * 4.0));
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    vec2 vUv = frag / res;
    // The component runs its time at 0.1 * speed (default 5).
    float t = wl_time() * 0.5;

    float pattern = silk(vUv, t);
    float rnd = noise(frag);
    float n = clamp((pattern - 0.2) / 0.8, 0.0, 1.0);

    vec2 step_uv = 1.0 / res;
    float gx = silk(vUv + vec2(step_uv.x, 0.0), t) - pattern;
    float gy = silk(vUv + vec2(0.0, step_uv.y), t) - pattern;
    vec3 normal = normalize(vec3(-gx * 26.0, -gy * 26.0, 1.0));
    vec3 to_light = normalize(vec3(-0.55, 0.7, 0.85));
    float relief = 1.0 + (dot(normal, to_light) - to_light.z) * 1.8 * P_DEPTH;
    vec3 halfway = normalize(to_light + vec3(0.0, 0.0, 1.0));
    float gloss = pow(max(dot(normal, halfway), 0.0), 48.0) * smoothstep(0.25, 0.85, n);
    float crest = pow(n, 5.0);
    float shine = (gloss * 0.85 + crest * 0.4) * P_SHEEN;

    float bloom = 0.0;
    if (P_GLOW > 0.0) {
        // Two rings of 6 taps stand in for the blurred mips 1..4.
        for (int i = 0; i < 6; i++) {
            float a = float(i) * 1.0471976;
            vec2 d = vec2(cos(a), sin(a)) / res;
            bloom += bright(vUv + d * 10.0, t) * 0.06;
            bloom += bright(vUv + d.yx * vec2(-1.0, 1.0) * 26.0, t) * 0.1;
        }
        bloom += bright(vUv, t) * 0.04;
    }

    vec3 result;
    if (P_LIGHT_MODE == 1) {
        float fold = smoothstep(0.28, 0.9, pattern);
        float specular = smoothstep(0.72, 0.98, pattern);
        vec3 shadow_color = P_COLOR * (0.72 - 0.12 * P_DEPTH);
        vec3 body_color = min(P_COLOR * 1.18, vec3(1.0));
        vec3 base = mix(shadow_color, body_color, fold) * mix(1.0, relief, 0.6);
        base = mix(base, vec3(1.0), clamp(specular * 0.92 + shine * 0.22 + bloom * P_GLOW * 0.2, 0.0, 1.0));
        float fine = noise(frag * 0.63 + vec2(17.0, 41.0));
        float grain_strength = clamp(P_NOISE * 0.038, 0.0, 0.16);
        result = clamp(base + (rnd + fine - 1.0) * grain_strength, 0.0, 1.0);
    } else {
        float grain = rnd / 15.0 * P_NOISE;
        float floor_value = mix(0.2, 0.03, P_DEPTH);
        float curve = P_DEPTH > 0.0 ? pow(n, 1.0 + 1.4 * P_DEPTH) : n;
        float shade = P_DEPTH > 0.0 ? floor_value + (1.0 - floor_value) * curve : pattern;
        vec3 tone = mix(P_COLOR, pow(P_COLOR, vec3(1.45)), (1.0 - n) * P_DEPTH);
        vec3 lit = tone * shade * relief - vec3(grain);
        vec3 tint = mix(P_COLOR, P_SHEEN_COLOR, 0.8);
        vec3 extra = tint * (shine + bloom * P_GLOW * 0.55);
        float blend = clamp((P_SHEEN + P_GLOW) * 4.0, 0.0, 1.0);
        result = mix(clamp(lit, 0.0, 1.0), clamp(soft_clip(lit + extra), 0.0, 1.0), blend);
    }
    gl_FragColor = vec4(result, 1.0);
}
