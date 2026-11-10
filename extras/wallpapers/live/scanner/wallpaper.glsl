// Scanner — ported from React Bits <Scanner /> (see ../LICENSE-REACTBITS.md).
// Same single-pass field as the original; the cursor lens, click pings and
// the power-on intro are dropped. Output is premultiplied, composited over
// the background colour.

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float signal(vec2 p, float t) {
    float w = sin(p.x * 1.3 + t * 0.7);
    w += sin(p.y * 1.7 - t * 0.52) * 0.8;
    w += sin((p.x + p.y) * 0.9 + t * 0.91) * 0.6;
    w += sin((p.x - p.y) * 1.53 - t * 0.63) * 0.42;
    return w * 0.35;
}

vec2 axes(vec2 p) {
#if P_DIRECTION == 0
    return vec2(p.y, p.x);
#elif P_DIRECTION == 1
    return vec2(p.x, -p.y);
#else
    return vec2((p.x + p.y) * 0.70710678, (p.x - p.y) * 0.70710678);
#endif
}

float coverage(float lc, float width) {
    float fw = max(fwidth(lc), 1e-5);
    float dist = abs(fract(lc + 0.5) - 0.5) / fw;
    float crowd = smoothstep(0.2, 0.55, fw);
    float cover = clamp(width * 0.5 + 0.5 - dist, 0.0, 1.0) * min(width, 1.0);
    return mix(cover, min(width * fw, 1.0), crowd);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float T = wl_time();
    // Wave clock runs at the component's default speed (0.5); the sweep at
    // its own sweep speed (phase shifted so the band is on screen at rest).
    float wave = T * 0.5;
#if P_REVERSE
    float sweepPos = -0.765 - T * P_SWEEP_SPEED;
#else
    float sweepPos = -0.765 + T * P_SWEEP_SPEED;
#endif
    float scale = max(P_SCALE, 0.001);

    vec2 uv = (frag * 2.0 - res) / res.y;
    vec2 p = uv / scale;
    vec2 ax = axes(p);

    float sig = signal(p * P_FREQUENCY, wave);
    float coord = ax.x + sig * P_RIPPLE;

    float u = fract(sweepPos - coord / max(P_SWEEP_WIDTH, 0.05));
    float tail = mix(11.0, 1.8, clamp(P_TRAIL, 0.0, 1.0));
    float lead = smoothstep(0.84, 1.0, u);
    lead *= lead;
    float sweep = max(exp(-u * tail), lead);
    float head = max(exp(-u * 26.0), lead * lead * lead);

    float lit = clamp(sweep * 0.86, 0.0, 1.6);
    float hot = clamp(head, 0.0, 1.0);

    float lc = coord * P_BAND_DENSITY;
    float width = P_LINE_WIDTH * (1.0 + lit * 1.4 + hot * 0.9);
    float thread = 0.62 + 0.38 * hash(vec2(floor(lc + 0.5), 3.7));
    float fw = max(fwidth(lc), 1e-5);
    float split = P_COLOR_SPREAD * (0.5 + lit * 1.3) * fw;
    float cg = coverage(lc, width);
    float cr = coverage(lc + split, width);
    float cb = coverage(lc - split, width);

    float dist = abs(fract(lc + 0.5) - 0.5) / fw;
    float crowd = smoothstep(0.2, 0.55, fw);
    float halo = exp(-dist / (3.0 + P_GLOW * 9.0)) * (1.0 - crowd) * P_GLOW * lit * 1.15;

    float base = 0.11;
    float level = (base + lit * (1.0 - base)) * thread * P_BRIGHTNESS;

    vec3 tone = mix(P_COLOR1, P_COLOR2, smoothstep(0.05, 0.75, lit));
    tone = mix(tone, P_COLOR3, hot);
    vec3 haloTone = mix(P_COLOR1, P_COLOR2, 0.65);

    float vx = wl_smoother((1.0 - abs(uv.x / (res.x / res.y))) / 0.6);
    float vy = wl_smoother((1.0 - abs(uv.y)) / 0.6);
    float vignette = mix(1.0, vx * vy, P_VIGNETTE);

    vec3 cover3 = vec3(cr, cg, cb);
    float alphaLine = max(max(cr, cg), cb) * level;
    vec3 colLine = tone * cover3 * level;

    float grain = (hash(frag + fract(T) * 91.7) - 0.5) * P_GRAIN;
    float scan = 1.0 - P_SCANLINES * 0.55 * (0.5 + 0.5 * cos(frag.y * 6.2831853 / 3.0));

#if P_LIGHT_MODE
    float ink = clamp(alphaLine * vignette * scan, 0.0, 1.0);
    float wash = clamp(halo * 0.28 * vignette, 0.0, 1.0);
    float alpha = clamp(ink + wash * (1.0 - ink) + grain * 0.5 * ink, 0.0, 1.0);
    vec3 col = (tone * ink + haloTone * wash * (1.0 - ink)) / max(ink + wash * (1.0 - ink), 1e-4);
    gl_FragColor = vec4(clamp(wl_over(P_PAPER, vec4(col * alpha, alpha)), 0.0, 1.0), 1.0);
#else
    vec3 col = (colLine + haloTone * halo) * vignette * scan;
    float alpha = clamp(max(alphaLine, halo) * vignette * scan + grain * max(alphaLine, 0.04), 0.0, 1.0);
    col = clamp(col + grain * 0.6 * alpha, 0.0, 1.0);
    col = min(col, vec3(alpha));
    gl_FragColor = vec4(clamp(wl_over(P_BACKGROUND, vec4(col, alpha)), 0.0, 1.0), 1.0);
#endif
}
