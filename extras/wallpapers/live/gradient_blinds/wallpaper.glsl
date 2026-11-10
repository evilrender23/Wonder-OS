// GradientBlinds — ported from React Bits <GradientBlinds /> (see ../LICENSE-REACTBITS.md).
// Single pass like the original. The cursor spotlight follows the idle
// «drift» path (or rests in the centre); hover tilt, click ripples and the
// intro are dropped. The page's `lighten` blend is applied over the
// background colour.

vec3 stop_color(int i) {
    if (i <= 0) return P_COLOR_1;
    if (i == 1) return P_COLOR_2;
    if (i == 2) return P_COLOR_3;
    return P_COLOR_4;
}

vec2 rotate2D(vec2 p, float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c) * p;
}

vec2 blind_space(vec2 uv0, vec2 res) {
    float aspect = res.x / res.y;
    vec2 p = uv0 * 2.0 - 1.0;
    p.x *= aspect;
    vec2 pr = rotate2D(p, P_ANGLE * 0.017453292);
    pr.x /= aspect;
    vec2 uv = pr * 0.5 + 0.5;
    if (abs(P_PERSPECTIVE) > 0.001) {
        float k = P_PERSPECTIVE * 2.2;
        uv.x = (exp(k * uv.x) - 1.0) / (exp(k) - 1.0);
    }
    if (P_DISTORT_AMOUNT > 0.0) {
        float w = 0.01 * P_DISTORT_AMOUNT;
        float a = uv.y * 6.0;
        float b = uv.x * 6.0;
        uv.x += sin(a) * w;
        uv.y += cos(b) * w;
    }
    return uv;
}

vec3 gradient_color(float t) {
    float tt = clamp(t, 0.0, 1.0);
    int count = P_COLORS;
    if (count < 2) count = 2;
    float scaled = tt * float(count - 1);
    float segf = min(floor(scaled), float(count - 2));
    float f = scaled - segf;
    int seg = int(segf);
    vec3 a = stop_color(seg);
    vec3 b = stop_color(seg + 1);
    if (P_COLORS < 2) b = a;
    if (tt >= 1.0) return b;
    return mix(a, b, f);
}

float grain_hash(vec2 p) {
    return wl_hash12(p);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    vec2 uv0 = frag / res;
    vec2 uvMod = blind_space(uv0, res);
    float sway_time = wl_time();

    float by_width = max(1.0, floor(res.x / max(P_BLIND_MIN_WIDTH, 1.0)));
    float count = max(min(float(P_BLIND_COUNT), by_width), 1.0);
    float stripePhase = uvMod.x * count;
    float pixel = max(fwidth(stripePhase), 1e-5);

    float t = uvMod.x;
    float shift = mod(sway_time * P_GRADIENT_SPEED * 0.06, 2.0);
    if (shift != 0.0) {
        t = 1.0 - abs(1.0 - fract((t + shift) * 0.5) * 2.0);
    }
#if P_MIRROR_GRADIENT
    t = 1.0 - abs(1.0 - 2.0 * fract(t));
#endif
    vec3 base = gradient_color(t);

    // Idle light: the component's «drift» path, or the centre.
    vec2 light = vec2(0.5);
#if P_IDLE == 0
    light.x = 0.5 + 0.22 * sin(sway_time * 0.23) + 0.05 * sin(sway_time * 0.61 + 1.3);
    light.y = 1.0 - (0.5 + 0.16 * sin(sway_time * 0.31 + 0.7) + 0.04 * sin(sway_time * 0.83));
#endif
    vec2 mouse = light * res;

    vec2 offset = light;
    float d = length(uv0 - offset);
    float r = max(P_SPOTLIGHT_RADIUS, 1e-4);
    float spot = (1.0 - 2.0 * pow(d / r, P_SPOTLIGHT_SOFTNESS)) * P_SPOTLIGHT_OPACITY;

    float slat = floor(stripePhase);
    float s = stripePhase - slat;
    float stripeAA = clamp(count * 1.25 / min(res.x, res.y), 0.001, 0.12);
    float edgeBlend = 1.0 - smoothstep(0.0, stripeAA, min(s, 1.0 - s));
    float across = mix(s, 0.5, edgeBlend);
    float ramp = P_SHINE_DIRECTION == 1 ? 1.0 - across : across;

    float unit = min(res.x, res.y);
    float reach = max(P_SPOTLIGHT_RADIUS, 0.05) * unit;
    vec2 toLight = mouse - frag;
    float nearLight = exp(-dot(toLight, toLight) / (reach * reach));

    float tilt = 0.0;
    if (P_SWAY > 0.0) {
        tilt += P_SWAY * 0.5 * sin(sway_time * 0.9 + slat * 0.55) * (0.6 + 0.4 * sin(sway_time * 0.37 + slat * 0.21));
    }

    float edge = ramp;
    ramp = pow(clamp(ramp, 0.0, 1.0), exp(tilt * 0.9));
    float crease = P_RELIEF * 0.38 * smoothstep(0.62, 1.0, ramp);
    float sheen = P_SHINE * exp(-pow((edge - 0.035) / 0.07, 2.0)) * (0.3 + 0.7 * nearLight) * (1.0 - edgeBlend);

    vec3 lit = spot > 0.0 ? P_LIGHT_COLOR * spot : vec3(spot);
    vec3 revealSignal = lit + base - vec3(ramp + crease);

    float grain_time = floor(sway_time * 24.0) * 17.13;
    vec3 col;
#if P_LIGHT_MODE
    vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    float peak = max(base.r, max(base.g, base.b));
    float low = min(base.r, min(base.g, base.b));
    float chroma = (peak - low) / max(peak, 0.0001);
    vec3 hue = base / max(peak, 0.0001);
    hue = clamp(mix(vec3(dot(hue, luma)), hue, 1.3), 0.0, 1.0);
    hue *= mix(1.0, 0.82, smoothstep(0.55, 0.9, dot(hue, luma)));
    vec3 tint = mix(base, hue, smoothstep(0.06, 0.28, chroma));
    vec3 revealed = clamp(revealSignal, 0.0, 1.0);
    float coverage = max(revealed.r, max(revealed.g, revealed.b));
    float stroke = 1.0 - smoothstep(pixel, pixel * 2.2, edge);
    col = mix(vec3(1.0), tint, 0.72 * smoothstep(0.0, 1.0, coverage));
    col = mix(col, tint * 0.85, stroke * clamp(coverage * 1.5, 0.0, 1.0) * 0.7);
    col = mix(col, vec3(1.0), clamp(sheen, 0.0, 1.0) * 0.5);
    float grain = max(0.5 * (grain_hash(frag + grain_time) + grain_hash(frag.yx + grain_time * 1.7)) - 0.5, 0.0);
    col = clamp(col - vec3(grain * P_NOISE * 0.12 * coverage), 0.0, 1.0);
#else
    vec3 haze = base * P_GLOW * 0.4 * smoothstep(0.0, 1.0, spot) * (0.4 + 0.6 * ramp);
    col = revealSignal + haze + mix(base, P_LIGHT_COLOR, 0.45) * sheen * 0.7;
    vec3 over = max(col - 0.8, 0.0);
    col = min(col, vec3(0.8)) + 0.2 * (1.0 - exp(-over / 0.2));
    float lum = dot(clamp(col, 0.0, 1.0), vec3(0.2126, 0.7152, 0.0722));
    float grain = 0.5 * (grain_hash(frag + grain_time) + grain_hash(frag.yx + grain_time * 1.7)) - 0.5;
    col += grain * P_NOISE * (0.45 + 0.55 * smoothstep(0.0, 0.5, lum));
    // mix-blend-mode: lighten over the page.
    col = max(clamp(col, 0.0, 1.0), P_BACKGROUND);
#endif
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
