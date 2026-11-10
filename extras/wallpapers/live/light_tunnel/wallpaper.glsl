// LightTunnel — ported from React Bits <LightTunnel /> (see ../LICENSE-REACTBITS.md).
// Already single pass upstream; the cursor steering, the click surge ring
// and the intro reveal are dropped.

const float TAU = 6.2831853;

vec3 ink_of(vec3 color, float depth_of_ink) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float peak = max(color.r, max(color.g, color.b));
    float chroma = (peak - min(color.r, min(color.g, color.b))) / max(peak, 0.001);
    return color * min(1.0, mix(depth_of_ink, depth_of_ink * 1.7, chroma) / max(luma, 0.001));
}

float hash(float n) {
    return fract(sin(n * 127.1 + P_VARIATION * 311.7) * 43758.5453);
}

vec2 bend_at(float depth, float drift) {
    float reach = 1.0 - exp(-max(depth, 0.0) * 0.55);
    vec2 wobble = vec2(sin(drift * 0.37 + depth * 0.31), cos(drift * 0.29 + depth * 0.23)) * (P_BEND * 0.45);
    return wobble * reach;
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float min_side = min(res.x, res.y);
    float scale = min_side * 0.5 * P_SIZE;
    // Component speed 0.1: time runs at speed, drift at speed + 0.2.
    float clock = wl_time();
    float time = clock * 0.1;
    float drift = clock * 0.3;
    float flow = P_FLOW_DIRECTION == 1 ? 1.0 : -1.0;
    float count = float(P_CABLE_COUNT);
    float swing = sin(drift * 0.9) * P_SWAY * 0.5 / TAU;
    float wave = P_WAVINESS * 0.15;
    float base_width = 0.25 + P_THICKNESS * 1.5;
    float pulse_width = 0.6 + P_PULSE_WIDTH * 0.9;
    float plen = P_PULSE_LENGTH * 1.2;
    float head_len = plen * (0.08 + P_PULSE_BLEND * 0.22);
    float tail_len = plen * (0.5 + P_PULSE_BLEND * 0.7);
    float chance = P_PULSE_DENSITY;
    float spacing_k = 0.4;
    float pulse_speed = P_PULSE_SPEED * 4.0;
    float halo = 1.2 + P_RIM_WIDTH * 8.0;
    float variance = P_COLOR_VARIANCE == 1 ? 1.0 : 0.0;
    float light_mode = P_LIGHT_MODE == 1 ? 1.0 : 0.0;

    vec2 p = (frag - 0.5 * res - vec2(P_CENTER_X, P_CENTER_Y) * min_side) / scale;
    float depth = -log(max(length(p), 1e-5));
    vec2 q = p;
    for (int i = 0; i < 4; i++) {
        q = p - bend_at(depth, drift);
        depth = -log(max(length(q), 1e-5));
    }
    float r = length(q);
    float px = r * scale;
    float turn = atan(q.y, q.x) / TAU;
    float s = turn - swing - wave * sin(depth * 1.2 + drift * 0.5);
    float cell = floor(s * count);
    float spacing = TAU * px / count;
    float near = smoothstep(0.55, 1.25, r);
    float width = base_width * (1.0 + near * 1.2);
    float soft = 0.55 + near * 0.9;
    float tight = halo * (1.0 + near);
    float wide = tight * 4.5;

    float lines = 0.0;
    float body = 0.0;
    vec3 glow = vec3(0.0);
    float strokes = 0.0;
    for (int k = -3; k <= 3; k++) {
        float i = cell + float(k);
        float id = mod(i, count);
        float h1 = hash(id);
        float h2 = hash(id + 17.0);
        float h3 = hash(id + 41.0);
        float h4 = hash(id + 73.0);
        float center = (i + 0.5 + (h1 - 0.5) * 0.55) / count;
        center += 0.12 * sin(depth * (0.8 + h2 * 0.9) + drift * (0.4 + h3 * 0.5) + h4 * TAU) / count;
        float d = abs(s - center) * TAU * px;
        float shade = 1.0 + variance * (h3 - 0.5) * 0.9;
        float reach = smoothstep(spacing * 2.4, spacing * 1.4, d);

        lines += smoothstep(width + soft, width - soft, d) * shade;
        body += exp(-d / max(spacing * 0.22, 0.001)) * shade * reach;

        float x = depth * spacing_k + time * pulse_speed * (0.55 + h2 * 0.6) * flow + h4 * 7.0;
        float lead = (fract(x) - 0.5) / spacing_k * flow;
        float lit = step(hash(id * 3.7 + floor(x) * 13.1), chance);
        float limit = 0.5 / spacing_k;
        float hl = max(-lead, 0.0) / head_len;
        float head = exp(-hl * hl) * smoothstep(-limit, -limit * 0.8, lead);
        float tail = exp(-max(lead, 0.0) / tail_len) * smoothstep(limit, limit * 0.55, lead);
        float pulse = (lead < 0.0 ? head : tail) * lit;
        float hot = pulse * (lead < 0.0 ? 1.0 : exp(-max(lead, 0.0) / (tail_len * 0.35)));

        float core = pulse_width * width + 0.35;
        float stroke = smoothstep(core + soft, core - soft, d);
        float bloom = (exp(-d / tight) * 0.7 + exp(-d / wide) * 0.18) * P_GLOW * reach;
        vec3 tint = mix(P_PULSE_COLOR, vec3(1.0), hot * 0.55 * (1.0 - light_mode));
        glow += tint * pulse * (stroke * 1.5 + bloom) * shade;
        strokes += pulse * stroke * shade;
    }

    float lod = smoothstep(4.0, 1.2, spacing);
    float average = min(2.0 * base_width / max(spacing, 0.001), 1.0);
    lines = mix(lines, average, lod);
    glow = mix(glow, P_PULSE_COLOR * average * 0.6 * chance, lod);
    strokes = mix(strokes, average * 0.6 * chance, lod);
    body = mix(body, 1.0, lod) * P_TUNNEL_OPACITY;

    float rr = r * 0.25;
    float fade = smoothstep(0.0, P_FADE_NEAR, rr) * smoothstep(P_FADE_FAR, P_FADE_FAR * 0.55, rr);
    fade *= smoothstep(-2.5, -1.9, depth);

    float grain = 1.0;
#if P_GRAIN
    grain += (fract(sin(dot(frag, vec2(12.9898, 78.233)) + time * 7.0) * 43758.5453) - 0.5) * P_GRAIN_INTENSITY * 4.0;
#endif

    vec3 back = P_BACKGROUND;
    vec4 premul;
#if P_LIGHT_MODE
    float hairline = clamp(lines * 0.26 + body * 0.2, 0.0, 1.0);
    float stroke_ink = clamp(strokes * 1.6, 0.0, 1.0);
    float haze = clamp(length(glow) * 0.12, 0.0, 0.25);
    float ink = clamp(hairline + stroke_ink + haze, 0.0, 1.0) * fade * grain;
    vec3 tone = mix(ink_of(P_CABLE_COLOR, 0.3), ink_of(P_PULSE_COLOR, 0.16), clamp(stroke_ink + haze * 2.0, 0.0, 1.0));
    tone = mix(tone, ink_of(P_TUNNEL_COLOR, 0.3), clamp(body * P_TUNNEL_OPACITY, 0.0, 1.0) * 0.5);
    premul = vec4(tone * ink, ink);
    // Ink is meant for light pages: keep a dark background readable.
    if (dot(back, vec3(0.2126, 0.7152, 0.0722)) < 0.5) back = vec3(0.965, 0.965, 0.97);
#else
    vec3 light = (P_CABLE_COLOR * lines * 0.12 + P_TUNNEL_COLOR * body * 0.35 + glow) * P_BRIGHTNESS * fade * grain;
    vec3 mapped = 1.0 - exp(-light * 1.4);
    premul = vec4(mapped, max(mapped.r, max(mapped.g, mapped.b)));
#endif
    gl_FragColor = vec4(wl_over(back, premul), 1.0);
}
