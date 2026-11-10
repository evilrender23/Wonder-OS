// MagicRings — ported from React Bits <MagicRings /> (see ../LICENSE-REACTBITS.md).
// Single pass: the half-res glow buffer and the instanced ring strips are
// evaluated per pixel for every ring (the strip's dFdx stretch comes from the
// analytic Jacobian of the tilt projection). Hover, mouse follow/parallax,
// click bursts and the intro are dropped.

const float CYCLE = 3.45;
const float STAGGER = 0.5;
const float DEPTH = 2.6;

float hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float angle_of(vec2 v) {
    vec2 a = abs(v);
    float t = min(a.x, a.y) / max(max(a.x, a.y), 1e-6);
    float r = t * (0.5 + 0.1738 * (1.0 - t));
    return a.y > a.x ? 1.0 - r : r;
}

vec3 prism_of(vec3 tint) {
    return mix(tint, vec3(dot(tint, vec3(0.3333))), 0.65) * 0.85;
}

vec4 tone_map(vec3 light) {
    float peak = max(max(light.r, light.g), light.b);
    if (peak <= 0.0) return vec4(0.0);
    float level = 1.0 - exp(-peak);
    vec3 color = mix(light * (level / peak), vec3(level), smoothstep(1.4, 3.6, peak) * 0.4);
    return vec4(color, level);
}

float coverage(float pixels, float half_width) {
    return clamp(max(half_width, 0.5) - pixels + 0.5, 0.0, 1.0) * min(half_width * 2.0, 1.0);
}

float smooth01(float a, float b, float x) {
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Screen offset (units of the short side) -> ring plane, through the
// inverse of the component's tilt homography (lean about the x axis).
vec2 to_plane(vec2 s, float cx, float sx) {
    float y = s.y / (cx + s.y * sx / DEPTH);
    float w = 1.0 - sx * y / DEPTH;
    return vec2(s.x * w, y);
}

vec3 tint_of(float m) {
#if P_LIGHT_MODE
    vec3 a = P_COLOR;
    vec3 b = P_COLOR_TWO;
#else
    vec3 a = wl_to_linear(P_COLOR);
    vec3 b = wl_to_linear(P_COLOR_TWO);
#endif
    return mix(a, b, m);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float unit = max(1.0, min(res.x, res.y));
    float time = wl_time() - 2.4;
    float clock = wl_time();
    float count = float(P_RING_COUNT);

    float tilt = radians(P_TILT);
    float cx = cos(tilt);
    float sx = sin(tilt);
    vec2 s = (frag - 0.5 * res) / unit;
    float wcheck = cx + s.y * sx / DEPTH;
    vec2 base = to_plane(s, cx, sx);
    vec2 base_dx = to_plane(s + vec2(1.0 / unit, 0.0), cx, sx) - base;
    vec2 base_dy = to_plane(s + vec2(0.0, 1.0 / unit), cx, sx) - base;
    float reach = length(base);

    float glow_scale = (P_LIGHT_MODE == 1 ? 0.3 : 1.0) * (1.0 - P_DISPERSION * 0.45);
    float outer = 0.012 + 0.033 * P_GLOW;
    float inner_amount = 0.5 * min(1.0, P_GLOW) * glow_scale;
    float outer_amount = 0.9 * P_GLOW * glow_scale;
    float range = outer * 4.5 + 0.008;
    float split = P_DISPERSION * 0.03;
    float limit = range + split;
    float width = P_LINE_THICKNESS;
    float rotation = radians(P_ROTATION);
    float spin_base = P_SPIN * time * 0.6;

    vec3 glow_light = vec3(0.0);
    float glow_alpha = 0.0;
    vec4 lines = vec4(0.0);
    bool valid = wcheck > 0.0;

    for (int i = 0; i < 12; i++) {
        if (i >= P_RING_COUNT || !valid) break;
        float fi = float(i);
        float m = count > 1.0 ? fi / (count - 1.0) : 0.0;
        float radius;
        float life;
        float virt;
#if P_MOTION == 1
        float span = count * P_RADIUS_STEP + P_EXPANSION;
        float period = CYCLE * max(1.0, count) * 0.32;
        float progress = fract(time / period + fi / count);
        life = smooth01(0.0, 0.12, progress) * (1.0 - smooth01(0.62, 1.0, progress));
        virt = progress * max(1.0, count - 1.0);
        radius = P_BASE_RADIUS + progress * span;
        m = progress;
#elif P_MOTION == 2
        float wave = sin(time / 4.0 * 6.2831853 - fi * 0.55);
        radius = P_BASE_RADIUS + fi * P_RADIUS_STEP + P_EXPANSION * 0.5 * (1.0 + wave);
        life = 0.78 + 0.22 * wave;
        virt = fi;
#else
        float tl = mod(time - fi * STAGGER, CYCLE);
        life = min(smooth01(0.0, 0.7, tl), 1.0 - smooth01(0.5, 3.25, tl));
        radius = P_BASE_RADIUS + fi * P_RADIUS_STEP + (1.0 - pow(1.0 - tl / CYCLE, 2.0)) * P_EXPANSION;
        virt = fi;
#endif
        if (life <= 0.002) continue;
        radius = max(radius, 0.001);
        float cut = pow(P_TAPER, virt);
        float gain = 10.0 * radius * cut * cut * cut;
        float max_a = gain > 6.2 ? pow(6.2 / gain, 1.0 / 3.0) : 2.0;
        float odd = mod(fi, 2.0) > 0.5 ? -1.0 : 1.0;
        float angle = rotation + spin_base * odd * (1.0 + fi * 0.15);
        vec2 fr = vec2(cos(angle), sin(angle));
        vec3 tint = tint_of(m);

        vec2 w = base;
        float len = reach;
        float d = abs(len - radius);
        if (d > limit + 0.03) continue;
        float a = angle_of(vec2(dot(w, fr), fr.x * w.y - fr.y * w.x));
        if (a > max_a) continue;
        float energy = life * max(exp(-gain * a * a * a) - 0.002, 0.0);

        // Glow buffer.
        if (d <= limit) {
            float off = split * radius;
            vec3 halo;
            vec3 dd = vec3(abs(len - radius - off), d, abs(len - radius + off));
            if (P_DISPERSION <= 0.0) dd = vec3(d);
            vec3 fall = exp(-dd / outer);
            vec3 tight = fall * fall;
            tight *= tight;
            vec3 window = clamp(1.0 - (dd - range * 0.45) / (range * 0.55), 0.0, 1.0);
            halo = (inner_amount * tight + outer_amount * fall) * window * window;
#if P_LIGHT_MODE
            float layer = clamp(energy * dot(halo, vec3(0.3333)), 0.0, 1.0);
            glow_light = tint * layer + glow_light * (1.0 - layer);
            glow_alpha = layer + glow_alpha * (1.0 - layer);
#else
            vec3 gt = P_DISPERSION > 0.0 ? prism_of(tint) : tint;
            glow_light += gt * energy * halo;
#endif
        }

        // Ring strip (drawn on top, blended per ring).
        vec2 grad = vec2(dot(w, base_dx), dot(w, base_dy)) / max(len, 1e-6);
        float stretch = 1.0 / max(length(grad), 1e-6);
        float half_width = width * max(1.0 - a, 0.5);
        float pixels = d * stretch;
        float off = split * radius;
        vec3 core = vec3(coverage(abs(len - radius - off) * stretch, half_width),
                         coverage(pixels, half_width),
                         coverage(abs(len - radius + off) * stretch, half_width));
        if (P_DISPERSION <= 0.0) core = vec3(core.g);
        energy *= step(a, max_a);
#if P_LIGHT_MODE
        float hot = core.g * clamp(1.0 - pixels / max(half_width * 0.35, 0.3), 0.0, 1.0);
        float layer = energy * max(max(core.r, core.g), core.b);
        if (layer > 0.0) {
            vec3 color = P_DISPERSION > 0.0 ? clamp(1.0 - (1.0 - tint) * core * 1.4, 0.0, 1.0) : tint;
            color = mix(color, vec3(1.0), hot * 0.7);
            vec4 src = vec4(color * layer, layer);
            lines = src + lines * (1.0 - src.a);
        }
#else
        vec3 lt = P_DISPERSION > 0.0 ? prism_of(tint) : tint;
        vec4 src = tone_map(lt * energy * core * 1.7);
        if (src.a > 0.0) {
            lines.rgb = src.rgb + lines.rgb * (1.0 - src.rgb);
            lines.a = src.a + lines.a * (1.0 - src.a);
        }
#endif
    }

    float grain = 1.0;
    if (P_GRAIN > 0.0) grain += P_GRAIN * (hash(frag + fract(clock * 7.31) * 413.0) - 0.5) * 1.8;
    vec4 col = vec4(0.0);
#if P_LIGHT_MODE
    float ga = clamp(glow_alpha * grain, 0.0, 1.0);
    if (ga > 0.0) col = vec4(min(glow_light * grain, vec3(ga)), ga);
    col = lines + col * (1.0 - lines.a);
#else
    vec4 g = tone_map(glow_light * grain);
    if (g.a > 0.0) col = vec4(min(g.rgb, vec3(g.a)), g.a);
    col.rgb = lines.rgb + col.rgb * (1.0 - lines.rgb);
    col.a = lines.a + col.a * (1.0 - lines.a);
#endif
    vec3 back = P_BACKGROUND;
#if P_LIGHT_MODE
    // Ink is meant for light pages: keep a dark background readable.
    if (dot(back, vec3(0.2126, 0.7152, 0.0722)) < 0.5) back = vec3(0.965, 0.965, 0.97);
#endif
    gl_FragColor = vec4(wl_over(back, col), 1.0);
}
