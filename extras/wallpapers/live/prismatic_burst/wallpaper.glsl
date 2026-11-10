// Prismatic Burst — ported from React Bits <PrismaticBurst /> (see ../LICENSE-REACTBITS.md).
// Same volumetric ray march as the original, rendered at full resolution in
// one pass. The 4-level bloom chain becomes a softened copy of the ray
// pattern accumulated in the same march (an angular blur). Cursor lean,
// click pulses, the "hover" animation and the intro are dropped.

#if P_QUALITY == 0
const int STEPS = 18;
#elif P_QUALITY == 1
const int STEPS = 26;
#else
const int STEPS = 44;
#endif

float g_time;

float hash21(vec2 p) {
    p = floor(p);
    float f = 52.9829189 * fract(dot(p, vec2(0.065, 0.005)));
    return fract(f);
}

float layeredNoise(vec2 fragPx) {
    vec2 p = mod(fragPx + vec2(g_time * 30.0, -g_time * 21.0), 1024.0);
    vec2 q = mat2(0.8, -0.5, 0.5, 0.8) * p;
    float n = 0.0;
    n += 0.40 * hash21(q);
    n += 0.25 * hash21(q * 2.0 + 17.0);
    n += 0.20 * hash21(q * 4.0 + 47.0);
    n += 0.10 * hash21(q * 8.0 + 113.0);
    n += 0.05 * hash21(q * 16.0 + 191.0);
    return n;
}

float edgeFade(vec2 frag, vec2 res, vec2 offset, float centerFade) {
    vec2 toC = frag - 0.5 * res - offset;
    float r = length(toC) / (0.5 * min(res.x, res.y) * centerFade);
    float x = clamp(r, 0.0, 1.0);
    float q = x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
    float s = q * 0.5;
    s = pow(s, 1.5);
    float tail = 1.0 - pow(1.0 - s, 2.0);
    s = mix(s, tail, 0.2);
    float dn = (layeredNoise(frag * 0.15) - 0.5) * 0.0015 * s;
    return clamp(s + dn, 0.0, 1.0);
}

vec2 rot2(vec2 v, float a) {
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c) * v;
}

float bendAngle(vec3 q, float t) {
    return 0.8 * sin(q.x * 0.55 + t * 0.6) + 0.7 * sin(q.y * 0.50 - t * 0.5) + 0.6 * sin(q.z * 0.60 + t * 0.7);
}

vec3 grad_color(int i) {
    if (i <= 0) return P_COLOR_1;
    if (i == 1) return P_COLOR_2;
    if (i == 2) return P_COLOR_3;
    return P_COLOR_4;
}

// Linear-filtered 1 x N gradient texture with clamp-to-edge, like the original.
vec3 gradient(float u) {
    float cnt = float(P_COLOR_COUNT);
    float x = clamp(u * cnt - 0.5, 0.0, cnt - 1.0);
    float fi = floor(x);
    int i = int(fi);
    return mix(grad_color(i), grad_color(i + 1 < P_COLOR_COUNT ? i + 1 : i), x - fi);
}

mat3 rotX(float a) { float c = cos(a); float s = sin(a); return mat3(1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c); }
mat3 rotY(float a) { float c = cos(a); float s = sin(a); return mat3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c); }
mat3 rotZ(float a) { float c = cos(a); float s = sin(a); return mat3(c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0); }

float smootherstep01(float t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    g_time = wl_time();
    float t = wl_time() * 0.5;
    float flow = wl_time() * P_FLOW * 1.2;
    vec2 offset = vec2(P_OFFSET_X, P_OFFSET_Y);
    float zoom = max(P_ZOOM, 0.05);
    float rayWidth = clamp(P_RAY_WIDTH, 0.2, 2.5);
    float centerFade = clamp(P_CENTER_FADE, 0.5, 3.0) * max(1.0, zoom);

#if P_ANIMATION == 0
    float a0 = t * 0.2;
    float c0 = cos(a0);
    mat3 orient = mat3(c0, 0.0, cos(a0 + 11.0), 0.0, 1.0, 0.0, cos(a0 + 33.0), 0.0, c0);
#else
    mat3 orient = rotZ(t * 0.17) * rotY(t * 0.21) * rotX(t * 0.31);
#endif

    float jitterAmp = 0.1 * clamp(P_GRAIN, 0.0, 1.0);
    vec2 centered = 2.0 * (frag - offset) - res;
    vec3 dir = normalize(vec3(centered, res.y * zoom));
    float marchT = 0.0;
    vec3 col = vec3(0.0);
    vec3 soft = vec3(0.0);
    float n = layeredNoise(frag);
    float amp = clamp(P_DISTORT, 0.0, 50.0) * 0.15;
    float edge = 0.5 - 0.35 * (rayWidth - 1.0);
    mat2 twist = mat2(1.0);
    if (P_TWIST != 0.0) {
        float a = P_TWIST * length(centered) / res.y;
        float c = cos(a);
        float s = sin(a);
        twist = mat2(c, s, -s, c);
    }
    float stepScale = 44.0 / float(STEPS);

    for (int i = 0; i < STEPS; ++i) {
        vec3 P = marchT * dir;
        P.z -= 2.0;
        float rad = length(P);
        vec3 Pl = P * (10.0 / max(rad, 1e-6));
        if (P_TWIST != 0.0) Pl.xy = twist * Pl.xy;
        Pl = orient * Pl;

        float stepLen = (min(rad - 0.3, n * jitterAmp) + 0.1) * stepScale;

        vec3 Pb = Pl;
        if (amp > 0.0) {
            float grow = smoothstep(0.35, 3.0, marchT);
            float a1 = amp * grow * bendAngle(Pl * 0.6, t);
            float a2 = 0.5 * amp * grow * bendAngle(Pl.zyx * 0.5 + 3.1, t * 0.9);
            Pb.xz = rot2(Pb.xz, a1);
            Pb.xy = rot2(Pb.xy, a2);
        }

        float v = sin(Pb.x + cos(Pb.y) * cos(Pb.z)) * sin(Pb.z + sin(Pb.y) * cos(Pb.x + t));
        float rayPattern = smoothstep(edge, edge + 0.2, v);
        float softPattern = smoothstep(edge - 0.55, edge + 0.75, v);

        if (P_RAY_COUNT > 0) {
            float ang = atan(Pb.y, Pb.x);
            float comb = 0.5 + 0.5 * cos(float(P_RAY_COUNT) * ang);
            comb = pow(comb, 3.0);
            float k = smoothstep(0.15, 0.95, comb);
            rayPattern *= k;
            softPattern *= mix(0.35, 1.0, k);
        }

        float depth = marchT - flow;
        vec3 spectral;
#if P_COLOR_COUNT > 0
        float saw = fract(depth * 0.25);
        float tRay = saw * saw * (3.0 - 2.0 * saw);
        spectral = 2.0 * gradient(clamp(tRay, 0.0, 1.0));
#else
        spectral = 1.0 + cos(depth * 3.0 + vec3(0.0, 1.0, 2.0));
#endif

        vec3 contrib = (0.05 / (0.4 + stepLen / stepScale)) * stepScale * smoothstep(5.0, 0.0, rad) * spectral;
        col += contrib * rayPattern;
        soft += contrib * softPattern;
        marchT += stepLen;
    }

    float fadeC = edgeFade(frag, res, offset, centerFade);
    col *= fadeC * P_INTENSITY;
    soft *= fadeC * P_INTENSITY;

#if P_LIGHT_MODE
    col += soft * P_GLOW * 0.35 * 0.25;
#else
    col += soft * P_GLOW * 0.35;
#endif
    col = clamp(col, 0.0, 1.0);

    vec2 uv = frag / res;
    float mask = 1.0;
    float fedge = P_FADE * 0.5;
    if (fedge > 0.0) {
        vec2 fu = clamp(uv, 0.0, 1.0);
        mask *= smootherstep01(fu.x / fedge) * smootherstep01((1.0 - fu.x) / fedge);
        mask *= smootherstep01(fu.y / fedge) * smootherstep01((1.0 - fu.y) / fedge);
    }

    vec4 premul;
#if P_LIGHT_MODE
    float energy = max(max(col.r, col.g), col.b);
    vec3 hue = col / max(energy, 0.0001);
    float neutral = min(hue.r, min(hue.g, hue.b));
    hue = max(hue - vec3(neutral * 0.68), vec3(0.0));
    hue /= max(max(hue.r, max(hue.g, hue.b)), 0.0001);
    vec3 pigment = mix(hue, hue * hue, 0.24) * 0.64;
    float coverage = smoothstep(0.001, 0.32, energy);
    coverage = pow(coverage, 0.72) * 0.92 * mask;
    premul = vec4(pigment * coverage, coverage);
#else
    col *= mask;
    premul = vec4(col, max(col.r, max(col.g, col.b)));
#endif
    gl_FragColor = vec4(clamp(wl_over(P_BACKGROUND, premul), 0.0, 1.0), 1.0);
}
