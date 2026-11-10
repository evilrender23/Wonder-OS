// LiquidEther — ported from React Bits <LiquidEther /> (see ../LICENSE-REACTBITS.md).
// The component runs a stable-fluids simulation stirred by an automatic
// "cursor" that wanders over the screen. Single pass: the velocity field is
// rebuilt analytically from the recent stretch of that wandering path — each
// segment leaves a jet along its direction that fades with the persistence
// half-life, widens as it diffuses and is swirled by a noise flow — and is
// shown through the same speed -> palette mapping. Pointer, clicks and the
// intro sweep are dropped.

#define SEGMENTS 28

float le_hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float le_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(le_hash(i), le_hash(i + vec2(1.0, 0.0)), u.x),
               mix(le_hash(i + vec2(0.0, 1.0)), le_hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Divergence-free swirl (curl of a noise potential), unit-ish amplitude.
float potential(vec2 q) {
    return le_noise(q) + 0.5 * le_noise(q * 2.03 + 7.1) + 0.25 * le_noise(q * 4.01 + 3.7);
}

vec2 swirl(vec2 p, float t) {
    float e = 0.04;
    vec2 q = p + vec2(t * 0.11, -t * 0.07);
    float n = potential(q);
    return vec2(potential(q + vec2(0.0, e)) - n, -(potential(q + vec2(e, 0.0)) - n)) / e;
}

vec3 palette(float level) {
    vec3 c0 = wl_to_linear(P_COLOR_1);
    vec3 c1 = wl_to_linear(P_COLOR_2);
    vec3 c2 = wl_to_linear(P_COLOR_3);
    float t = clamp(level * 3.0 - 0.5, 0.0, 2.0);
    float k = fract(t);
    k = k * k * (3.0 - 2.0 * k);
    if (t >= 2.0) return c2;
    return t < 1.0 ? mix(c0, c1, k) : mix(c1, c2, k);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float time = wl_time();
    float reach = max(res.x, res.y);

    // Motion half-life from persistence (keepFor), and how much history
    // still shows: about three half-lives.
    float half_life = 0.35 + P_PERSISTENCE * P_PERSISTENCE * 12.0;
    float window = clamp(half_life * 2.6, 1.2, 20.0);
    float dt = window / float(SEGMENTS);
    float rate = P_AUTO_SPEED * 1.6;

    float radius = P_CURSOR_SIZE * (reach / 1920.0);
    float sigma0 = radius / sqrt(12.0) * (1.0 + 1.5 * P_VISCOSITY);
    // Velocity a passing stroke leaves behind (frames x force x kernel).
    float strength = 0.45 * P_AUTO_INTENSITY * radius * 0.7236 * 60.0;

    // Swirl the lookup point: older jets have been mixed further.
    vec2 flow = swirl(frag / reach * 3.2, time * (0.4 + P_AUTO_SPEED)) * reach * 0.018 * (1.0 - 0.6 * P_VISCOSITY);

    vec2 velocity = vec2(0.0);
    // Walk the path backwards in time. Each sine of `wander` (and the gust)
    // is a phasor rotated by a fixed step, so the loop needs no sin/cos.
    float phase0 = time * rate + 37.0;
    float step = dt * rate;
    vec2 z1 = vec2(cos(phase0 * 0.83 + 1.3), sin(phase0 * 0.83 + 1.3));
    vec2 z2 = vec2(cos(phase0 * 1.71 + 0.4), sin(phase0 * 1.71 + 0.4));
    vec2 z3 = vec2(cos(phase0 * 1.07 + 2.1), sin(phase0 * 1.07 + 2.1));
    vec2 z4 = vec2(cos(phase0 * 0.61 + 0.7), sin(phase0 * 0.61 + 0.7));
    float gp = (phase0 - 0.5 * step) * 2.6;
    vec2 z5 = vec2(cos(gp), sin(gp));
    vec2 r1 = vec2(cos(step * 0.83), -sin(step * 0.83));
    vec2 r2 = vec2(cos(step * 1.71), -sin(step * 1.71));
    vec2 r3 = vec2(cos(step * 1.07), -sin(step * 1.07));
    vec2 r4 = vec2(cos(step * 0.61), -sin(step * 0.61));
    vec2 r5 = vec2(cos(step * 2.6), -sin(step * 2.6));
    float decay = exp2(-dt / half_life);
    float keep = exp2(-0.5 * dt / half_life);
    vec2 amp = 0.41 * res;
    vec2 newer = (0.5 + vec2(0.62 * z1.y + 0.38 * z2.y, 0.6 * z3.y + 0.4 * z4.x) * 0.41) * res;
    for (int i = 0; i < SEGMENTS; i++) {
        float age = (float(i) + 0.5) * dt;
        z1 = vec2(z1.x * r1.x - z1.y * r1.y, z1.x * r1.y + z1.y * r1.x);
        z2 = vec2(z2.x * r2.x - z2.y * r2.y, z2.x * r2.y + z2.y * r2.x);
        z3 = vec2(z3.x * r3.x - z3.y * r3.y, z3.x * r3.y + z3.y * r3.x);
        z4 = vec2(z4.x * r4.x - z4.y * r4.y, z4.x * r4.y + z4.y * r4.x);
        vec2 older = 0.5 * res + vec2(0.62 * z1.y + 0.38 * z2.y, 0.6 * z3.y + 0.4 * z4.x) * amp;
        vec2 seg = newer - older;
        float len2 = max(dot(seg, seg), 1e-3);

        float spread = 1.0 + age * (0.55 + 0.6 * P_VISCOSITY);
        float sigma = sigma0 * spread;
        vec2 p = frag + flow * min(0.3 + age * 0.6, 3.0);
        float h = clamp(dot(p - older, seg) / len2, 0.0, 1.0);
        vec2 d = p - older - seg * h;
        float q = dot(d, d) / (2.0 * sigma * sigma);
        if (q < 9.0) {
            float gust = 0.3 + 0.7 * (0.5 + 0.5 * z5.y) * (0.5 + 0.5 * z5.y);
            // The newest stretch is still building up behind the stirrer.
            float fresh = i == 0 ? 1.0 - 0.7 * h * h : 1.0;
            velocity += seg * inversesqrt(len2) * (strength * gust * keep * fresh / spread) * exp(-q);
        }
        z5 = vec2(z5.x * r5.x - z5.y * r5.y, z5.x * r5.y + z5.y * r5.x);
        keep *= decay;
        newer = older;
    }

    float speed = length(velocity) * 1.25 * P_INTENSITY / (0.84 * reach);
    float level = speed < 0.75 ? speed : 0.75 + 0.25 * wl_tanh((speed - 0.75) * 4.0);
    vec3 ink = palette(level);
    float alpha = level;
#if P_LIGHT_MODE
    float hollow = smoothstep(0.7, 1.6, speed);
    alpha = smoothstep(0.08, 0.5, level) * mix(0.85, 0.1, hollow);
    vec3 page = vec3(1.0) - P_BACKGROUND * 0.35;
    vec3 color = wl_over(page, vec4(ink * alpha, alpha));
#else
    float hot = 1.0 - exp(-max(speed - 0.75, 0.0) * 1.2);
    ink = mix(ink, min(ink * 1.8 + 0.06, vec3(1.0)), hot);
    vec3 color = wl_over(P_BACKGROUND, vec4(ink * alpha, alpha));
#endif
    float grain = fract(52.9829189 * fract(dot(floor(frag) + 0.5, vec2(0.06711056, 0.00583715))));
    color += (grain - 0.5) / 255.0;
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
