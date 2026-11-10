// Aurora — ported from React Bits <Aurora /> (see ../LICENSE-REACTBITS.md).
// Same simplex curtains and OKLCH colour ramp as the original; the stops are
// converted to OKLCH (with hue unwrapping) in the shader instead of in JS.
// Pointer reach and the intro unfurl are dropped.

vec3 permute(vec3 x) {
    return mod(((x * 34.0) + 1.0) * x, 289.0);
}

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

float smootherstep_(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

vec3 oklabToLinear(vec3 c) {
    float l = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
    float m = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
    float s = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    return vec3(
        4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

float cbrt(float x) {
    return sign(x) * pow(abs(x), 1.0 / 3.0);
}

vec3 toOklch(vec3 srgb) {
    vec3 c = wl_to_linear(srgb);
    float l = cbrt(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b);
    float m = cbrt(0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b);
    float s = cbrt(0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    vec3 lab = vec3(
        0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s);
    float ch = length(lab.yz);
    float hue = ch > 1e-6 ? atan(lab.z, lab.y) : 0.0;
    return vec3(lab.x, ch, hue);
}

vec3 S0; vec3 S1; vec3 S2; vec3 S3; vec3 S4;

vec3 stop(int i) {
    if (i <= 0) return S0;
    if (i == 1) return S1;
    if (i == 2) return S2;
    if (i == 3) return S3;
    return S4;
}

const float TAU = 6.28318530718;

vec3 unwrap(vec3 st, inout float previous) {
    float hue = st.y > 0.02 ? st.z : previous;
    hue -= TAU * floor((hue - previous + 3.14159265) / TAU);
    previous = hue;
    return vec3(st.x, st.y, hue);
}

void build_palette() {
    vec3 a = toOklch(P_COLOR_1);
    vec3 b = toOklch(P_COLOR_2);
    vec3 c = toOklch(P_COLOR_3);
    vec3 d = toOklch(P_COLOR_4);
    vec3 e = toOklch(P_COLOR_5);
    int n = P_COLOR_COUNT;
    float anchor = 0.0;
    if (a.y > 0.02) anchor = a.z;
    else if (b.y > 0.02) anchor = b.z;
    else if (n > 2 && c.y > 0.02) anchor = c.z;
    else if (n > 3 && d.y > 0.02) anchor = d.z;
    else if (n > 4 && e.y > 0.02) anchor = e.z;
    float prev = anchor;
    S0 = unwrap(a, prev);
    S1 = unwrap(b, prev);
    S2 = unwrap(c, prev);
    S3 = unwrap(d, prev);
    S4 = unwrap(e, prev);
}

vec3 ramp(float t) {
    float cnt = float(P_COLOR_COUNT);
    float x = clamp(t, 0.0, 1.0) * (cnt - 1.0);
    float fi = min(floor(x), cnt - 2.0);
    int i = int(fi);
    float f = x - fi;
    f = f * f * (3.0 - 2.0 * f);
    vec3 lch = mix(stop(i), stop(i + 1), f);
#if P_LIGHT_MODE
    lch.x = min(lch.x, 0.7);
#endif
    return wl_to_srgb(oklabToLinear(vec3(lch.x, lch.y * cos(lch.z), lch.y * sin(lch.z))));
}

void main() {
    build_palette();
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float y = 1.0 - frag.y / res.y;
#if P_DIRECTION == 1
    y = 1.0 - y;
#endif
    float u = frag.x / res.x;
    float X = (frag.x - res.x * 0.5) / res.y;
    float t = wl_time();

    vec3 glow = vec3(0.0);
    float cover = 0.0;
    for (int k = 0; k < 3; k++) {
        if (k >= P_LAYERS) break;
        float layer = float(k);
        float seed = layer * 13.7;
        float pace = t * (1.0 - layer * 0.18);
        float warp = snoise(vec2(X * 0.45 * P_SCALE + pace * 0.02 + seed, pace * 0.05 + seed));
        float wave = snoise(vec2(X * 0.75 * P_SCALE + warp * 0.35 - pace * 0.03 + seed, pace * 0.07 - seed)) * 0.72
            + snoise(vec2(X * 1.6 * P_SCALE + warp * 0.5 + pace * 0.04 - seed, pace * 0.1 + seed * 0.5)) * 0.28;
        float hem = P_HEIGHT + layer * 0.07 + wave * 0.15 * P_AMPLITUDE;

        float soft = (0.04 + P_BLEND * 0.16) * (1.0 + layer * 0.9);
        float rise = hem - y;
        float low = -soft - P_GLOW * 0.14;
        float high = 0.26 + soft * 0.6;
        float body = pow(smootherstep_(low, high, rise), 1.5 + P_GLOW * 0.6);
        float lobes = 0.86 + 0.14 * snoise(vec2(X * 0.9 * P_SCALE - pace * 0.025 + seed, y * 0.3 + pace * 0.04));
        float alpha = clamp(body * lobes * (layer < 0.5 ? 1.0 : 0.32 / max(layer, 1.0)), 0.0, 1.0);

        vec3 tint = ramp(u + 0.05 * sin(pace * 0.08 + layer * 2.3) + layer * 0.1 + max(rise, 0.0) * 0.05);
        glow = 1.0 - (1.0 - glow) * (1.0 - tint * alpha);
        cover = 1.0 - (1.0 - cover) * (1.0 - alpha);
    }

    float strength = clamp(P_BRIGHTNESS, 0.0, 2.0);
    glow *= min(strength, 1.0);
    cover *= min(strength, 1.0);
    glow = 1.0 - pow(max(1.0 - glow, 0.0), vec3(max(strength, 1.0)));
    float dither = (wl_hash12(frag) - 0.5) / 255.0;

    vec3 col;
#if P_LIGHT_MODE
    vec3 tintc = cover > 0.0001 ? glow / cover : vec3(0.0);
    cover *= smootherstep_(0.0, 0.25, cover);
    float a = cover * 0.7;
    col = wl_over(P_BACKGROUND, vec4(tintc * a, a)) + dither;
#else
    col = wl_over(P_BACKGROUND, vec4(glow, cover)) + dither;
#endif
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
