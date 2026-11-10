// Plasma — ported from React Bits <Plasma /> (see ../LICENSE-REACTBITS.md).
// Single pass: the volume march (rendered at reduced quality into a float
// target in the original) runs per pixel at full resolution and feeds the
// composite directly. Cursor orbit/heat and the intro are dropped.

const float C11 = 0.0044256979881;   // cos(11)
const float S11 = -0.9999902065507;  // sin(11)
const float C33 = -0.0132767472230;  // cos(33)
const float S33 = 0.9999118601072;   // sin(33)

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float smootherEdge(float edge, float x) {
    float t = clamp(x / max(edge, 0.0001), 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float plasmaTime(float clock) {
    // timeOf(): forward / reverse / ping-pong (10 s eased segments).
#if P_DIRECTION == 1
    return -clock * 0.4;
#elif P_DIRECTION == 2
    float span = 10.0;
    float segment = mod(clock, span) / span;
    float eased = segment * segment * (3.0 - 2.0 * segment);
    float even = mod(floor(clock / span), 2.0);
    return (even < 0.5 ? eased : 1.0 - eased) * span * 0.4;
#else
    return clock * 0.4;
#endif
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float clock = wl_time();
    float uTime = plasmaTime(clock);
    float sharpness = 100.0 - P_SOFTNESS * 90.0;
    float wave = 0.4 * P_WAVE;

    vec2 c = (frag - 0.5 * res) / P_SCALE;
    vec3 rd = normalize(vec3(c, res.y));
    vec3 ro = vec3(0.0, 0.0, -4.0);
    float z = 0.0;
    float density = 0.0;
    float band = 0.0;
    for (int i = 0; i < 60; i++) {
        vec3 p = ro + rd * z;
        vec3 s = p;
        float d = p.y - uTime;
        p.x += wave * (1.0 + p.y) * sin(d + p.x * 0.1) * cos(0.34 * d + p.x * 0.05);
        // mat2(cos(a + vec4(0, 11, 33, 0))) from a single sin/cos pair.
        float a = p.y * P_TWIST - uTime;
        float ca = cos(a);
        float sa = sin(a);
        float m1 = ca * C11 - sa * S11;
        float m2 = ca * C33 - sa * S33;
        vec2 q = p.xz * mat2(ca, m1, m2, ca);
        float shell = abs(sqrt(length(q * q)) - 0.25 * (5.0 + s.y)) / 3.0;
        float stepLength = shell + 0.0008;
        z += stepLength;
        vec2 dq = s.xz - q;
        float phase = s.y + q.y * 0.5 + s.z - length(dq);
        float weight = (1.0 + sin(phase + 8.0)) * exp(-shell * sharpness) * stepLength;
        density += weight;
        band += weight * (0.5 + 0.5 * sin(phase * 0.5 + 1.0));
    }

    // Composite.
    vec3 base = pow(P_COLOR, vec3(2.2));
    vec3 highlight = base + (1.0 - base) * 0.85;
    float gain = 4.8 * P_BRIGHTNESS;
    float shine = 0.025 * P_SHINE;
    float dens = density * gain;
    float bandMix = clamp(band / max(density, 0.00001), 0.0, 1.0);
    vec3 tint = mix(base, highlight, bandMix * 0.35);
    vec3 hdr = tint * dens + highlight * dens * dens * shine;
    float peak = max(hdr.r, max(hdr.g, hdr.b));
    float level = 1.0 - exp(-peak);
    float hot = smoothstep(1.0, 3.0, peak);
    vec2 uv = frag / res;
    float edge = P_FADE * 0.5;
    float mask = 1.0;
    if (P_FADE > 0.0) {
        mask = smootherEdge(edge, uv.x) * smootherEdge(edge, 1.0 - uv.x) * smootherEdge(edge, uv.y) * smootherEdge(edge, 1.0 - uv.y);
    }
    float grain = (hash(frag + fract(clock * 7.31) * 113.0) - 0.5) * P_GRAIN;

    vec4 premul;
#if P_LIGHT_MODE
    float tone = dot(base, vec3(0.2126, 0.7152, 0.0722));
    float depth = min(1.0, 0.16 / max(tone, 0.001));
    vec3 ink = pow(min(vec3(1.0), base * depth), vec3(1.0 / 2.2));
    vec3 col = mix(ink, vec3(1.0), hot * 0.75);
    float alpha = clamp(pow(level, 0.8) * 0.9 * (1.0 + grain), 0.0, 1.0) * mask;
    premul = vec4(col * alpha, alpha);
    // Light mode sits on a light page: keep a light background.
    vec3 bg = dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722)) > 0.5 ? P_BACKGROUND : vec3(1.0);
#else
    vec3 hue = hdr / max(peak, 0.0001);
    vec3 col = mix(hue * level, vec3(level), hot * 0.6);
    col = pow(col, vec3(1.0 / 2.2));
    col = clamp(col + grain * level, 0.0, 1.0) * mask;
    premul = vec4(col, max(col.r, max(col.g, col.b)));
    vec3 bg = P_BACKGROUND;
#endif
    gl_FragColor = vec4(wl_over(bg, premul), 1.0);
}
