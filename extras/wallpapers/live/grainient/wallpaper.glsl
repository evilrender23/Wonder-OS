// Grainient — ported from React Bits <Grainient /> (see ../LICENSE-REACTBITS.md).
// Single pass: the half-resolution field and the grain composite are fused;
// cursor stirring, click swirls and the intro bloom are dropped.

vec2 g_dir;
float g_extent;
float g_aspect;
float g_time;
vec2 g_seed;

vec2 ghash2(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.103, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz) * q.zy) * 2.0 - 1.0;
}

float gnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(ghash2(i), f);
    float b = dot(ghash2(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    float c = dot(ghash2(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    float d = dot(ghash2(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
    return 0.5 + 0.5 * mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec2 drift(vec2 p, float t) {
    vec2 w1 = vec2(
        gnoise(p * 0.85 + vec2(t * 0.11, t * 0.07)),
        gnoise(p * 0.85 + vec2(4.1, 1.7) - vec2(t * 0.08, t * 0.12)));
    vec2 w2 = vec2(
        gnoise(p * 1.55 + w1 * 1.8 + vec2(1.3, 7.1) + t * 0.05),
        gnoise(p * 1.55 + w1 * 1.8 + vec2(6.4, 2.2) - t * 0.06));
    return (w1 - 0.5) * 1.1 + (w2 - 0.5) * 0.55;
}

vec2 warp(vec2 p) {
    vec2 q = p / P_SCALE;
#if P_TYPE == 1
    float turn = g_time * 0.35;
    q = mat2(cos(turn), sin(turn), -sin(turn), cos(turn)) * q;
#else
    q += vec2(-g_dir.y, g_dir.x) * g_time * 0.45;
#endif
    return p + drift(q + g_seed, g_time) * P_FLOW * P_SCALE;
}

float coord(vec2 p) {
    vec2 w = warp(p);
#if P_TYPE == 1
    return 1.0 - length(w) * 1.1;
#else
    return dot(w, g_dir) / g_extent + 0.5;
#endif
}

vec3 to_lab(vec3 srgb) {
    vec3 c = wl_to_linear(srgb);
    float l = pow(max(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b, 0.0), 1.0 / 3.0);
    float m = pow(max(0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b, 0.0), 1.0 / 3.0);
    float s = pow(max(0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b, 0.0), 1.0 / 3.0);
    return vec3(
        0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s);
}

vec3 lab_to_rgb(vec3 lab) {
    float l = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    return vec3(
        4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

vec3 color_at(int i) {
    if (i == 0) return P_COLOR_1;
    if (i == 1) return P_COLOR_2;
    if (i == 2) return P_COLOR_3;
    if (i == 3) return P_COLOR_4;
    return P_COLOR_5;
}

// Fill the component's OKLab ramp. Light mode rebuilds the palette: colours
// as dark as the first become white and the rest run in reverse, lifted.
void build_ramp(out vec3 R[5]) {
    vec3 L[5];
    for (int i = 0; i < 5; i++) L[i] = to_lab(color_at(i));
#if P_LIGHT_MODE
    float base_l = L[0].x;
    int total = 0;
    for (int k = 0; k < 5; k++) {
        if (k < P_COUNT && abs(L[k].x - base_l) >= 0.1) total++;
    }
    int rank = 0;
    for (int i = 0; i < 5; i++) {
        R[i] = vec3(1.0, 0.0, 0.0);
        if (abs(L[i].x - base_l) >= 0.1) {
            int want = total - 1 - rank;
            int seen = 0;
            vec3 pick = L[i];
            for (int k = 0; k < 5; k++) {
                if (k < P_COUNT && abs(L[k].x - base_l) >= 0.1) {
                    if (seen == want) pick = L[k];
                    seen++;
                }
            }
            R[i] = vec3(pick.x + (1.0 - pick.x) * 0.2, pick.yz * 0.8);
            rank++;
        }
    }
#else
    for (int i = 0; i < 5; i++) R[i] = L[i];
#endif
}

float ghash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float clump(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(ghash(i), ghash(i + vec2(1.0, 0.0)), u.x),
               mix(ghash(i + vec2(0.0, 1.0)), ghash(i + vec2(1.0, 1.0)), u.x), u.y);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    vec2 uv = frag / res;
    g_aspect = res.x / res.y;
    // time += dt * speed * 0.16 in the component.
    g_time = wl_time() * 0.16;
    float rad = radians(P_ANGLE);
    g_dir = vec2(sin(rad), cos(rad));
    g_extent = g_aspect * abs(g_dir.x) + abs(g_dir.y);
    g_seed = vec2(3.17, 1.91) * P_VARIATION;

    vec2 p = (uv - 0.5) * vec2(g_aspect, 1.0);
    float raw = coord(p);
    float s = raw;
    if (P_BANDS > 1.0) {
        s *= P_BANDS;
        float cycle = floor(s);
        s = fract(s);
        if (mod(cycle, 2.0) > 0.5) s = 1.0 - s;
    }
    s = clamp(s, 0.0, 1.0);
    float count = float(P_COUNT);
    float scaled = s * (count - 1.0);
    float index = min(floor(scaled), count - 2.0);
    float f = scaled - index;
    float edge = max(P_SOFTNESS * 0.5, fwidth(scaled) * 0.75);
    float shaped = smoothstep(0.5 - edge, 0.5 + edge, f);
    vec3 R[5];
    build_ramp(R);
    vec3 from = R[0];
    vec3 to = R[1];
    for (int i = 1; i < 4; i++) {
        if (float(i) == index) {
            from = R[i];
            to = R[i + 1];
        }
    }
    vec3 base = R[0];
    vec3 lab = mix(from, to, shaped);
    if (P_SILK > 0.0) {
        vec2 grad = vec2(coord(p + vec2(0.004, 0.0)) - raw, coord(p + vec2(0.0, 0.004)) - raw) / 0.004;
        vec2 slope = grad * sin(3.14159265 * clamp(raw, 0.0, 1.0) * P_BANDS) * P_SILK * 4.0;
        vec3 normal = normalize(vec3(-slope, 1.0));
        vec3 light = normalize(vec3(-g_dir.y, g_dir.x, 1.2));
        float diffuse = dot(normal, light) - light.z;
        lab = base + (lab - base) * (1.0 + (diffuse * 0.35 + max(diffuse, 0.0) * diffuse * 0.6) * P_SILK);
    }
    lab.x = (lab.x - 0.55) * P_CONTRAST + 0.55;
    lab.yz *= P_SATURATION;
    vec3 color = wl_to_srgb(lab_to_rgb(lab));

    // Film grain (the composite pass), re-seeded 24 times a second.
#if P_GRAIN_ANIMATED
    float frame = mod(floor(wl_time() * 24.0), 97.0);
#else
    float frame = 0.0;
#endif
    vec2 pixel = floor(frame) * vec2(17.0, 59.0) + floor(frag);
    float fine = ghash(pixel) + ghash(pixel + 7.13) - 1.0;
    float coarse = (clump(pixel / max(P_GRAIN_SIZE, 1.0)) - 0.5) * 2.0;
    float grain = mix(fine, coarse, clamp(P_GRAIN_SIZE - 1.0, 0.0, 1.0) * 0.8);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color += grain * P_GRAIN_AMOUNT * (0.45 + 2.2 * luma * (1.0 - luma));
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
