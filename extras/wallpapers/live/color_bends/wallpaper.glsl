// ColorBends — ported from React Bits <ColorBends /> (see ../LICENSE-REACTBITS.md).
// Same single-pass field as the original; pointer parallax/influence are
// fixed at the resting pointer (screen centre), so the default pull to the
// centre (mouseInfluence 1) is kept.

vec3 bend_color(int i) {
    if (i == 0) return P_COLOR_1;
    if (i == 1) return P_COLOR_2;
    if (i == 2) return P_COLOR_3;
    return P_COLOR_4;
}

float band(vec2 s, float t, float fi) {
    float fr = P_FREQUENCY;
    vec2 r = sin(1.5 * (s.yx * fr) + 2.0 * cos(s * fr));
    float m0 = length(r + sin(5.0 * r.y * fr - 3.0 * t + fi) / 4.0);
    float kBelow = clamp(P_WARP_STRENGTH, 0.0, 1.0);
    float kMix = pow(kBelow, 0.3);
    float gain = 1.0 + max(P_WARP_STRENGTH - 1.0, 0.0);
    vec2 disp = (r - s) * kBelow;
    vec2 warped = s + disp * gain;
    float m1 = length(warped + sin(5.0 * warped.y * fr - 3.0 * t + fi) / 4.0);
    float m = mix(m0, m1, kMix);
    return 1.0 - exp(-P_BAND_WIDTH / exp(P_BAND_WIDTH * m));
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    vec2 vUv = frag / res;
    float clock = wl_time();
    // Component default speed 0.2.
    float t = clock * 0.2;

    float deg = P_ROTATION + P_AUTO_ROTATE * clock;
    float rad = deg * 0.017453292;
    vec2 rot = vec2(cos(rad), sin(rad));

    vec2 p = vUv * 2.0 - 1.0;
    vec2 rp = vec2(p.x * rot.x - p.y * rot.y, p.x * rot.y + p.y * rot.x);
    vec2 q = vec2(rp.x * (res.x / res.y), rp.y);
    q /= max(P_SCALE, 0.0001);
    q /= 0.5 + 0.2 * dot(q, q);
    q += 0.2 * cos(t) - 7.56;
    q += -rp * 0.2;

    for (int j = 0; j < 4; j++) {
        if (j >= P_ITERATIONS - 1) break;
        vec2 rr = sin(1.5 * (q.yx * P_FREQUENCY) + 2.0 * cos(q * P_FREQUENCY));
        q += (rr - q) * 0.15;
    }

    vec3 col = vec3(0.0);
    float a = 1.0;
    vec2 s = q;
    if (P_COLORS > 0) {
        vec3 sumCol = vec3(0.0);
        float cover = 0.0;
        for (int i = 0; i < 4; i++) {
            if (i >= P_COLORS) break;
            s -= 0.01;
            float w = band(s, t, float(i));
            sumCol += bend_color(i) * w;
            cover = max(cover, w);
        }
        col = clamp(sumCol, 0.0, 1.0);
        a = cover;
    } else {
        s -= 0.01;
        col.r = band(s, t, 0.0);
        s -= 0.01;
        col.g = band(s, t, 1.0);
        s -= 0.01;
        col.b = band(s, t, 2.0);
        a = max(max(col.r, col.g), col.b);
    }

    col *= P_INTENSITY;
    if (P_NOISE > 0.0001) {
        float n = fract(sin(dot(frag + vec2(clock), vec2(12.9898, 78.233))) * 43758.5453123);
        col += (n - 0.5) * P_NOISE;
        col = clamp(col, 0.0, 1.0);
    }
#if P_TRANSPARENT
    gl_FragColor = vec4(wl_over(P_BACKGROUND, vec4(col * a, a)), 1.0);
#else
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
#endif
}
