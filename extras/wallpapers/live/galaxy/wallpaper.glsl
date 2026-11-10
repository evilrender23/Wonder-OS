// Galaxy — ported from React Bits <Galaxy /> (see ../LICENSE-REACTBITS.md).
// Already a single-pass shader; pointer repulsion, click warp and the warp
// intro are dropped. The automatic centre repulsion is kept.

#define STAR_COLOR_CUTOFF 0.2
#define PERIOD 3.0

float Hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float tri(float x) {
    return abs(fract(x) * 2.0 - 1.0);
}

float tris(float x) {
    float t = fract(x);
    return 1.0 - smoothstep(0.0, 1.0, abs(2.0 * t - 1.0));
}

float trisn(float x) {
    float t = fract(x);
    return 2.0 * (1.0 - smoothstep(0.0, 1.0, abs(2.0 * t - 1.0))) - 1.0;
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 palette_color(float pick) {
    if (pick < 1.0) return P_COLOR_1;
    if (pick < 2.0) return P_COLOR_2;
    if (pick < 3.0) return P_COLOR_3;
    return P_COLOR_4;
}

float Star(vec2 uv, float flare, float invReach, float glowAmt, float flareGain) {
    float d = length(uv);
    float m = glowAmt / max(d, 1e-4);
    float rays = smoothstep(0.0, 1.0, 1.0 - abs(uv.x * uv.y * 1000.0));
    m += rays * flare * flareGain;
    uv *= mat2(0.7071, -0.7071, 0.7071, 0.7071);
    rays = smoothstep(0.0, 1.0, 1.0 - abs(uv.x * uv.y * 1000.0));
    m += rays * 0.3 * flare * flareGain;
    m *= smoothstep(1.0, 0.2, d * invReach);
    return m;
}

vec3 StarLayer(vec2 uv, float t, float gloss, float pushCells, float spread,
               vec2 repelAt, float glowAmt, float flareGain) {
    vec3 col = vec3(0.0);
    vec2 gv = fract(uv) - 0.5;
    vec2 id = floor(uv);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offset = vec2(float(x), float(y));
            vec2 si = id + offset;
            float seed = Hash21(si);
            float size = fract(seed * 345.32);
            float glossLocal = tri(gloss / (PERIOD * seed + 1.0));
            float flareSize = smoothstep(0.9, 1.0, size) * glossLocal;

            vec2 pad = vec2(tris(seed * 34.0 + t / 10.0), tris(seed * 38.0 + t / 30.0)) - 0.5;
            vec2 shift = vec2(0.0);
            float invReach = 1.0;
            if (pushCells > 0.0) {
                vec2 away = si + 0.5 + pad - repelAt;
                float dist = length(away);
                float q = dist * spread;
                float cells = pushCells * exp(-q * q);
                float moved = cells * inversesqrt(1.0 + cells * cells * 2.78);
                shift = away * (moved / max(dist, 0.0001));
                invReach = 1.0 / (1.0 - moved);
            }
            float star = Star(gv - offset - pad - shift, flareSize, invReach, glowAmt, flareGain);
            if (star <= 0.0) continue;
            vec3 base;
#if P_USE_PALETTE
            float pick = Hash21(si + 7.0) * float(P_PALETTE_SIZE);
            base = palette_color(min(floor(pick), float(P_PALETTE_SIZE - 1)));
#else
            float red = smoothstep(STAR_COLOR_CUTOFF, 1.0, Hash21(si + 1.0)) + STAR_COLOR_CUTOFF;
            float blu = smoothstep(STAR_COLOR_CUTOFF, 1.0, Hash21(si + 3.0)) + STAR_COLOR_CUTOFF;
            float grn = min(red, blu) * seed;
            base = vec3(red, grn, blu);
            float hue = atan(base.g - base.r, base.b - base.r) / (2.0 * 3.14159) + 0.5;
            hue = fract(hue + P_HUE_SHIFT / 360.0);
            float sat = length(base - vec3(dot(base, vec3(0.299, 0.587, 0.114)))) * P_SATURATION;
            float val = max(max(base.r, base.g), base.b);
            base = hsv2rgb(vec3(hue, sat, val));
#endif

            float twinkle = trisn(t + seed * 6.2831) * 0.5 + 1.0;
            twinkle = mix(1.0, twinkle, P_TWINKLE);
            col += star * twinkle * size * base;
        }
    }
    return col;
}

void main() {
    vec2 res = wl_res();
    float t = wl_time();
    vec2 uv = (wl_frag() - 0.5 * res) / res.y;

    // starSpeed / 10 drives the flight; direction flips it.
    float flight = P_STAR_SPEED / 10.0 * (P_DIRECTION == 1 ? -1.0 : 1.0);
    float gloss = t * flight;
    float depthPhase = t * flight;
    float angle = t * P_ROTATION_SPEED;

    float strength = P_CENTER_REPULSION;
    float push = strength * 0.05;
    float radius = 0.16 + 0.04 * strength;

    float ca = cos(angle);
    float sa = sin(angle);
    uv = mat2(ca, -sa, sa, ca) * uv;

    float glowAmt = 0.05 * P_GLOW * P_STAR_SIZE;
    float flareGain = P_GLOW * P_FLARE;

    vec3 col = vec3(0.0);
    float layers = float(P_LAYERS);
    for (int k = 0; k < 8; k++) {
        if (k >= P_LAYERS) break;
        float i = float(k) / layers;
        float depth = fract(i + depthPhase);
        float scale = mix(20.0 * P_DENSITY, 0.5 * P_DENSITY, depth);
        float fade = depth * smoothstep(1.0, 0.9, depth);
        vec2 offset = vec2(i * 453.32);
        col += StarLayer(uv * scale + offset, t, gloss, push * scale, 1.0 / (scale * radius),
                         offset, glowAmt, flareGain) * fade;
    }

    vec3 result;
    if (P_LIGHT_MODE == 1) {
        float lumBg = dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722));
        vec3 paper = lumBg > 0.5 ? P_BACKGROUND : vec3(1.0);
        vec3 lit = col * smoothstep(0.0, 0.3, length(col));
        float energy = max(max(lit.r, lit.g), lit.b);
        vec3 tint = lit / max(energy, 0.0001);
        vec3 ink = tint * (0.16 / max(dot(tint, vec3(0.2126, 0.7152, 0.0722)), 0.16));
        float coverage = pow(min(energy * 1.3, 1.0), 1.3) * 0.92;
        result = mix(paper, ink, coverage);
    } else {
        float alpha = min(smoothstep(0.0, 0.3, length(col)), 1.0);
        result = wl_over(P_BACKGROUND, vec4(col * alpha, alpha));
    }
    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
