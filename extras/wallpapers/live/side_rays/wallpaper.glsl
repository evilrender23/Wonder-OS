// SideRays — ported from React Bits <SideRays /> (see ../LICENSE-REACTBITS.md).
// Already single pass upstream: fwidth() becomes its analytic value, the
// pointer lean/focus and the intro are dropped.

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}

float shafts(float u, float t, float seed, float aa) {
    float sway = 0.4 * sin(t * 0.31 + seed) + 0.24 * sin(t * 0.17 + seed * 2.3);
    float n = 0.7 * noise(vec2(u + sway + seed, t * 0.3))
            + 0.3 * noise(vec2(u * 2.4 + sway * 1.5 + seed * 1.7, t * 0.45 + 3.0));
    float width = mix(0.5, 0.1, P_SHARPNESS);
    float s = mix(smoothstep(0.5 - width, 0.5 + width, n), 0.5, aa);
    return mix(0.05, 1.0, s);
}

float motes(vec2 p, float cell, float t, float seed, float px) {
    p += vec2(t * 0.021, -t * 0.013) + seed * 7.31;
    vec2 grid = p / cell;
    vec2 id = floor(grid);
    vec2 f = grid - id;
    float h = hash(id + seed);
    if (h > 0.5) return 0.0;
    vec2 center = 0.25 + 0.5 * vec2(hash(id + 1.7 + seed), hash(id + 3.1 + seed));
    center += 0.1 * vec2(sin(t * 0.7 + h * 31.0), cos(t * 0.6 + h * 23.0));
    float size = mix(0.035, 0.09, hash(id + 5.3 + seed));
    // length(fwidth(grid)) for grid = q / cell, q in view-height units.
    float edge = 1.41421356 * px / cell * 0.7;
    float twinkle = 0.55 + 0.45 * sin(t * 1.9 + h * 41.0);
    return (1.0 - smoothstep(size - edge, size + edge, length(f - center))) * twinkle;
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float px = 1.0 / res.y;
    float aspect = res.x / res.y;
    // speed 2.5 * 0.4 upstream; it also starts at a random time.
    float t = wl_time() + 37.0;

    // origin: 0 top-right, 1 top-left, 2 bottom-right, 3 bottom-left, 4 right, 5 left
    float flip_x = (P_ORIGIN == 1 || P_ORIGIN == 3 || P_ORIGIN == 5) ? 1.0 : 0.0;
    float flip_y = (P_ORIGIN == 2 || P_ORIGIN == 3) ? 1.0 : 0.0;
    float side = (P_ORIGIN >= 4) ? 1.0 : 0.0;

    float spread = P_SPREAD * 0.225;
    float intensity = P_INTENSITY * 0.75;
    float glow_amount = P_GLOW * 1.2;
    float tilt = radians(P_TILT);

    vec2 q = (frag - 0.5 * res) / res.y;
    if (flip_x > 0.5) q.x = -q.x;
    if (flip_y > 0.5) q.y = -q.y;
    vec2 source = side > 0.5 ? vec2(aspect * 0.5 + 0.42, 0.3) : vec2(aspect * 0.5 + 0.1, 0.62);
    float heading = (side > 0.5 ? 3.42 : -2.48) + tilt + 0.035 * sin(t * 0.21);
    vec2 v = q - source;
    float r = length(v);
    float angle = mod(atan(v.y, v.x) - heading + 3.14159265, 6.28318531) - 3.14159265;
    float u = angle * P_DENSITY;
    // fwidth(u): one pixel spans px / r radians at distance r.
    float aa = smoothstep(0.12, 0.45, P_DENSITY * px / max(r, 1e-4) * 1.4);
    float offset = spread * 0.22;
    float fan_a = exp(-pow((angle + offset) / spread, 2.0) * 1.4);
    float fan_b = exp(-pow((angle - offset) / (spread * 1.15), 2.0) * 1.4);
    float light_a = fan_a * shafts(u, t, 0.0, aa);
    float light_b = fan_b * shafts(u * 0.83, t * 0.6, 17.0, aa);
    float reach = 1.0 / (1.0 + pow(r / 0.4, P_FALLOFF));
    vec3 glow_color = mix(P_RAY_COLOR1, P_RAY_COLOR2, P_BLEND);
    vec3 light = (P_RAY_COLOR1 * light_a * (1.0 - P_BLEND) + P_RAY_COLOR2 * light_b * P_BLEND) * 2.0 * reach;
    light += glow_color * glow_amount * exp(-pow(r / 0.4, 2.0));
    if (P_DUST > 0.0) {
        float sparkle = motes(q, 0.06, t, 0.0, px) + 0.7 * motes(q, 0.038, t * 0.8, 9.0, px);
        light += mix(glow_color, vec3(1.0), 0.6) * sparkle * (light_a + light_b) * reach * P_DUST * 2.5;
    }
    light *= intensity;
    float gray = dot(light, vec3(0.2126, 0.7152, 0.0722));
    light = max(mix(vec3(gray), light, P_SATURATION), 0.0);
    float peak = max(light.r, max(light.g, light.b));
    float grain = (fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;

    vec4 premul;
    vec3 back = P_BACKGROUND;
#if P_LIGHT_MODE
    vec3 ink = pow(light / max(peak, 1e-4), vec3(2.0)) * 0.75;
    float alpha = clamp(pow(1.0 - exp(-peak * 2.6), 2.5) * 0.9 + grain, 0.0, 1.0);
    premul = vec4(pow(ink, vec3(1.0 / 2.2)) * alpha, alpha);
    // Light mode is meant for light pages: keep a dark background readable.
    if (dot(back, vec3(0.2126, 0.7152, 0.0722)) < 0.5) back = vec3(0.965, 0.965, 0.97);
#else
    float mapped = 1.0 - exp(-peak);
    vec3 tone = light * (mapped / max(peak, 1e-4));
    tone = mix(tone, vec3(mapped), smoothstep(0.7, 1.0, mapped) * 0.55);
    vec3 shown = clamp(tone, 0.0, 1.0);
    float alpha = max(shown.r, max(shown.g, shown.b));
    vec3 color = clamp(shown * alpha + grain, 0.0, 1.0);
    premul = vec4(min(color, vec3(alpha)), alpha);
#endif
    gl_FragColor = vec4(wl_over(back, premul), 1.0);
}
