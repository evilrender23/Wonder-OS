// FloatingLines — ported from React Bits <FloatingLines /> (see ../LICENSE-REACTBITS.md).
// Same single-pass field; the per-line uniforms are computed in the shader
// and the line slope comes from the analytic gradient of the warped
// coordinates. Pointer bend, parallax, click swirl and intro are dropped.

const int MAX_LINES = 24;

vec3 to_srgb_22(vec3 c) {
    return pow(max(c, 0.0), vec3(1.0 / 2.2));
}

vec3 ink_of(vec3 color) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return color * min(1.0, 0.16 / max(luma, 0.001));
}

vec2 warp(vec2 p, float rotate, float flip) {
    float angle = rotate * log(length(p) + 1.0);
    vec2 r = p * mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    if (flip > 0.5) r.x = -r.x;
    return r;
}

// x, y, rotate of each wave (top, middle, bottom).
vec3 wave_of(int w) {
    if (w == 0) return vec3(10.0, 0.5, -0.4);
    if (w == 1) return vec3(5.0, 0.0, 0.2);
    return vec3(2.0, -0.7, -1.0);
}

// offset, step, light, flip.
vec4 shape_of(int w) {
    if (w == 0) return vec4(1.0, 0.2, 0.2, 1.0);
    if (w == 1) return vec4(2.0, 0.15, 1.0, 0.0);
    return vec4(1.5, 0.2, 0.32, 0.0);
}

float depth_of(int w) {
    if (w == 0) return 1.0;
    if (w == 1) return 0.0;
    return 0.6;
}

bool wave_on(int w) {
    if (w == 0) return P_TOP == 1;
    if (w == 1) return P_MIDDLE == 1;
    return P_BOTTOM == 1;
}

vec3 stop_color(int i) {
    if (i == 0) return pow(P_COLOR_1, vec3(2.2));
    if (i == 1) return pow(P_COLOR_2, vec3(2.2));
    return pow(P_COLOR_3, vec3(2.2));
}

vec3 line_color(float t) {
    int stops = P_COLORS;
    if (stops <= 1) return stop_color(0);
    float scaled = t * float(stops - 1);
    float from = min(floor(scaled), float(stops - 2));
    float f = clamp(scaled - from, 0.0, 1.0);
    float e = f * f * (3.0 - 2.0 * f);
    vec3 a = from < 0.5 ? stop_color(0) : stop_color(1);
    vec3 b = from < 0.5 ? stop_color(1) : stop_color(2);
    return mix(a, b, e);
}

vec3 paper() {
    vec3 bg = P_BACKGROUND;
    return dot(bg, vec3(0.2126, 0.7152, 0.0722)) > 0.5 ? bg : vec3(1.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float time = wl_time();

    vec2 screen = (2.0 * frag - res) / res.y;
    screen.y = -screen.y;

    vec3 light = vec3(0.0);
    vec3 bloom = vec3(0.0);
    vec3 inkColor = vec3(0.0);
    float inkAlpha = 0.0;
    float aa = 0.75;
    float tight = 1.0 / 2.6;
    float wide = 1.0 / 11.0;
    float uWidth = P_LINE_WIDTH * 0.5;
    float haloReach = 11.0 * log(160.0 * P_GLOW + 1.0);
    float spacing = P_LINE_STYLE == 2 ? 22.0 : 9.0;
    int count = P_LINE_COUNT;

    for (int w = 0; w < 3; w++) {
        if (!wave_on(w)) continue;
        vec3 wave = wave_of(w);
        vec4 shape = shape_of(w);
        vec2 r = warp(screen, wave.z, shape.w);
        vec2 rdx = dFdx(r);
        vec2 rdy = dFdy(r);
        float blur = P_DEPTH_OF_FIELD * depth_of(w);
        float width = uWidth * (1.0 + blur * 2.5);
        float soft = aa * (1.0 + blur * 7.0);
        float energy = (uWidth + aa) / (width + soft) * shape.z;
        float cut = haloReach + width * 2.0 + soft;
        float level = r.y + wave.y;

        for (int i = 0; i < MAX_LINES; i++) {
            if (i >= count) break;
            float fi = float(i);
            float offset = shape.x + shape.y * fi;
            float along = P_LINE_DISTANCE * 0.01 * fi + wave.x;
            float phase = along + offset + time * 0.1;
            float amp = sin(offset + time * 0.2) * 0.3 * P_AMPLITUDE;
            float y = sin(r.x + phase) * amp;
            float m = level - y;
            float dy_dx = cos(r.x + phase) * amp;
            vec2 grad = vec2(rdx.y - dy_dx * rdx.x, rdy.y - dy_dx * rdy.x);
            float d = abs(m) / max(length(grad), 1e-5);
            if (d < cut) {
                float gap = d;
                float size = width;
#if P_LINE_STYLE != 0
                float period = spacing / (res.y * 0.5);
                float cell = (fract((r.x + along) / period + fi * 0.37) - 0.5) * spacing;
                float span = P_LINE_STYLE == 2 ? max(spacing * 0.32 - width, 0.0) : 0.0;
                gap = length(vec2(max(abs(cell) - span, 0.0), d));
                size = P_LINE_STYLE == 2 ? width : width * 1.6;
#endif
                float core = smoothstep(size + soft, size - soft, gap);
                float fall = 1.0 - d / cut;
                float halo = (exp(-gap * tight) * 0.3 + exp(-d * wide) * 0.02 * fall * fall) * P_GLOW;
                vec3 color = line_color(count > 1 ? fi / float(count - 1) : 0.0);
                light += color * core * energy;
                bloom += color * halo * shape.z;
                float a = clamp(core * energy * 1.15 + halo * shape.z * 0.12, 0.0, 1.0);
                inkColor = ink_of(color) * a + inkColor * (1.0 - a);
                inkAlpha = a + inkAlpha * (1.0 - a);
            }
        }
    }

    float edge = 1.0;
    if (P_EDGE_FADE > 0.0) {
        float u = frag.x / res.x;
        float fade = P_EDGE_FADE * 0.45;
        float left = smoothstep(0.0, fade, u);
        float right = smoothstep(0.0, fade, 1.0 - u);
        edge = left * left * (3.0 - 2.0 * left) * right * right * (3.0 - 2.0 * right);
    }

#if P_LIGHT_MODE
    float alpha = clamp(inkAlpha * edge, 0.0, 1.0);
    vec3 ink = to_srgb_22(inkColor / max(inkAlpha, 1e-4));
    gl_FragColor = vec4(mix(paper(), ink, alpha), 1.0);
#else
    vec3 capped = 0.28 * (1.0 - exp(-bloom / 0.28));
    vec3 glow = to_srgb_22(1.0 - exp(-(light + capped) * P_BRIGHTNESS * 1.2)) * edge;
    // mix-blend-mode: screen over the page.
    vec3 bg = P_BACKGROUND;
    gl_FragColor = vec4(bg + glow * (1.0 - bg), 1.0);
#endif
}
