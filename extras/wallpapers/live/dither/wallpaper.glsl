// Dither — ported from React Bits <Dither /> (see ../LICENSE-REACTBITS.md).
// The two passes (wave field rendered per dither cell, then the ordered
// dither) are fused: each pixel evaluates the wave at its cell corner, which
// is exactly what the low-res field texture held. Cursor trail, click
// ripples, the intro and the custom palette are dropped.

vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }
vec4 taylor_inv_sqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }
vec2 fade2(vec2 t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float cnoise(vec2 P) {
    vec4 Pi = floor(P.xyxy) + vec4(0.0, 0.0, 1.0, 1.0);
    vec4 Pf = fract(P.xyxy) - vec4(0.0, 0.0, 1.0, 1.0);
    Pi = mod289(Pi);
    vec4 ix = Pi.xzxz;
    vec4 iy = Pi.yyww;
    vec4 fx = Pf.xzxz;
    vec4 fy = Pf.yyww;
    vec4 i = permute(permute(ix) + iy);
    vec4 gx = fract(i * (1.0 / 41.0)) * 2.0 - 1.0;
    vec4 gy = abs(gx) - 0.5;
    vec4 tx = floor(gx + 0.5);
    gx = gx - tx;
    vec2 g00 = vec2(gx.x, gy.x);
    vec2 g10 = vec2(gx.y, gy.y);
    vec2 g01 = vec2(gx.z, gy.z);
    vec2 g11 = vec2(gx.w, gy.w);
    vec4 norm = taylor_inv_sqrt(vec4(dot(g00, g00), dot(g01, g01), dot(g10, g10), dot(g11, g11)));
    g00 *= norm.x;
    g01 *= norm.y;
    g10 *= norm.z;
    g11 *= norm.w;
    float n00 = dot(g00, vec2(fx.x, fy.x));
    float n10 = dot(g10, vec2(fx.y, fy.y));
    float n01 = dot(g01, vec2(fx.z, fy.z));
    float n11 = dot(g11, vec2(fx.w, fy.w));
    vec2 fade_xy = fade2(Pf.xy);
    vec2 nx = mix(vec2(n00, n01), vec2(n10, n11), fade_xy.x);
    return 2.3 * mix(nx.x, nx.y, fade_xy.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 1.0;
    for (int i = 0; i < 4; i++) {
        value += amp * abs(cnoise(p));
        p *= P_WAVE_FREQUENCY;
        amp *= P_WAVE_AMPLITUDE;
    }
    return value;
}

float pattern(vec2 p, float t) {
    // waveSpeed (default 0.05) is folded into the global speed.
    vec2 p2 = p - t * 0.05;
    return fbm(p + fbm(p2));
}

// 2x2 Bayer cells as the component's tables lay them out.
float b2(vec2 c) { return c.x * (2.0 - c.y) + 3.0 * c.y * (1.0 - c.x); }  // bayer2/bayer4
float b2t(vec2 c) { return c.x * (3.0 - 2.0 * c.y) + 2.0 * c.y * (1.0 - c.x); } // bayer8

float threshold(vec2 cell) {
#if P_DITHER_MODE == 1
    vec2 c = mod(cell, 4.0);
    return (4.0 * b2(mod(c, 2.0)) + b2(floor(c / 2.0))) / 16.0;
#elif P_DITHER_MODE == 2
    return b2(mod(cell, 2.0)) / 4.0;
#elif P_DITHER_MODE == 3
    vec2 r = vec2(cell.x + cell.y, cell.x - cell.y) * 0.70710678 / 4.0;
    return clamp(length(fract(r) - 0.5) * 1.41421356, 0.0, 0.999);
#elif P_DITHER_MODE == 4
    return abs(fract(cell.y / 4.0) - 0.5) * 2.0 * 0.999;
#elif P_DITHER_MODE == 5
    float a = abs(fract((cell.x + cell.y) / 6.0) - 0.5) * 2.0;
    float b = abs(fract((cell.x - cell.y) / 6.0) - 0.5) * 2.0;
    return min(a, b) * 0.999;
#elif P_DITHER_MODE == 6
    return fract(52.9829189 * fract(dot(cell, vec2(0.06711056, 0.00583715))));
#elif P_DITHER_MODE == 7
    return 0.25;
#else
    vec2 c = mod(cell, 8.0);
    return (16.0 * b2t(mod(c, 2.0)) + 4.0 * b2t(mod(floor(c / 2.0), 2.0)) + b2t(floor(c / 4.0))) / 64.0;
#endif
}

void main() {
    vec2 res = wl_res();
    float cell_px = float(P_PIXEL_SIZE);
    vec2 cell = floor(wl_frag() / cell_px);

    // Wave field, sampled at the cell corner like the low-res field pass.
    vec2 uv = cell * cell_px / res - 0.5;
    uv.x *= res.x / res.y;
    float f = pattern(uv / P_SCALE, wl_time());
    f = (f - 0.5) * P_CONTRAST + 0.5 + P_BRIGHTNESS;
    f = clamp(f, 0.0, 1.0);
    // The field texture is RGBA8 with a 16-bit split: keep that precision.
    f = floor(f * 65535.0 + 0.5) / 65535.0;

    float t = threshold(cell) - 0.25;
    vec3 wave = wl_to_linear(P_WAVE_COLOR);
    vec3 background = wl_to_linear(P_BACKGROUND_COLOR);
    float levels = max(float(P_COLOR_NUM), 1.01) - 1.0;
    vec3 lum_w = vec3(0.2126, 0.7152, 0.0722);

    float base = dot(background, lum_w);
    vec3 offset = (wave - vec3(base)) * f + t / levels;
    float luminance = dot(background + offset, lum_w);
    offset -= mix(0.2, 0.0, smoothstep(0.45, 0.8, luminance));
    bool rising = dot(wave, lum_w) >= base;
    float lower = rising ? 0.0 : -max(background.r, max(background.g, background.b));
    float upper = rising ? 1.0 - min(background.r, min(background.g, background.b)) : 0.0;
    offset = floor(clamp(offset, lower, upper) * levels + 0.5) / levels;
    gl_FragColor = vec4(wl_to_srgb(background + offset), 1.0);
}
