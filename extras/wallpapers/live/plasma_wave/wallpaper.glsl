// PlasmaWave — ported from React Bits <PlasmaWave /> (see ../LICENSE-REACTBITS.md).
// Single pass: the coarse/fine ray-march split collapses into one march per
// pixel, and the 4-level bloom chain is approximated by a soft halo derived
// from the march's closest approach to each ribbon (k), which falls off like
// the blurred light around them. Cursor swell/lens and the intro are dropped.

const float PI = 3.14159265;
const float TAU = 6.28318531;
const float HALF_PI = 1.5707963;

// Returns the light (rgb) and the field value (a); halo gets the soft glow.
vec4 trace_light(vec2 pixel, vec2 view, float time, out vec3 halo) {
    vec3 color1 = pow(P_COLOR1, vec3(2.2));
    vec3 color2 = pow(P_COLOR2, vec3(2.2));
    vec2 coord = pixel + vec2(P_X_OFFSET, P_Y_OFFSET) * view.y - 0.5 * view;
    float rot = radians(P_ROTATION);
    float c = cos(rot);
    float sn = sin(rot);
    coord = mat2(c, -sn, sn, c) * coord;
    vec3 u = normalize(vec3(coord / view.y, P_FOCAL_LENGTH));
    vec3 o = vec3(0.0, 0.0, -7.0);
    float t = time * PI;
    float t1 = t * 0.7;
    float t2 = t * 0.9;
    float t_speed1 = t * P_SPEED1;
    float t_speed2 = t * P_SPEED2 * (P_REVERSE2 == 1 ? -1.0 : 1.0);
    float s = 1.0;
    float d = 0.0;
    vec2 k = vec2(0.0);
    vec2 k_min = vec2(1e3);
    for (int i = 0; i < 14; ++i) {
        vec3 p = o + u * d;
        p.x -= 15.0;
        float px = p.x;
        float wob1 = P_BEND1 + sin(t1 + px * 0.8) * 0.1;
        float wob2 = P_BEND2 + cos(t2 + px * 1.1) * 0.1;
        vec2 a = sin(vec2(px, px + HALF_PI) + t_speed1) * wob1;
        vec2 b = cos(vec2(px, px + HALF_PI) + t_speed2) * wob2;
        float cut = px + P_THICKNESS;
        k.x = max(cut, length(p.yz - a) - P_THICKNESS);
        k.y = max(cut, length(p.yz - b) - P_THICKNESS);
        k_min = min(k_min, k);
        s = min(s, min(k.x, k.y));
        if (s < 0.001 || d > 300.0) break;
        d += s * 0.7;
    }
    vec3 raw = max(cos(d * TAU) - s * sqrt(d) - vec3(k, 0.0), 0.0);
    float field = max(raw.r, max(raw.g, raw.b));
    raw.gb += 0.1;
    raw = raw * 0.4 + raw.brg * 0.6 + raw * raw;
    float lum = dot(raw, vec3(0.299, 0.587, 0.114));
    float w1 = max(0.0, 1.0 - k.x * 2.0) + 0.0001;
    float w2 = max(0.0, 1.0 - k.y * 2.0) + 0.0001;
    vec3 base = (color1 * w1 + color2 * w2) / (w1 + w2);
    float soft = smoothstep(0.03, 0.36, field);
    float hot = smoothstep(0.32, 0.8, field);
    vec3 light = base * lum * 2.6 * soft + mix(base, vec3(1.0), 0.55) * hot * P_CORE * 2.0;
    // Bloom stand-in: light spilled by each ribbon at the closest approach.
    vec2 km = max(k_min, 0.0);
    vec2 spill = exp(-km * 3.2) * 0.75 + exp(-km * 0.9) * 0.25;
    halo = (color1 * spill.x + color2 * spill.y) * (1.0 + P_CORE * 0.6) * 0.9;
    return vec4(light, field);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float time = wl_time();
    vec3 halo;
    vec4 source = trace_light(frag, res, time, halo);
    float exposure = 2.4 * P_BRIGHTNESS;
    vec3 hdr = source.rgb * exposure;
    hdr += halo * exposure * P_GLOW * 0.18;
    vec2 uv = frag / res;
    float edge = P_FADE * 0.5;
    float mask = wl_smoother(uv.x / max(edge, 0.0001)) * wl_smoother((1.0 - uv.x) / max(edge, 0.0001))
               * wl_smoother(uv.y / max(edge, 0.0001)) * wl_smoother((1.0 - uv.y) / max(edge, 0.0001));
    vec2 gp = fract(frag * vec2(123.34, 456.21) + fract(time * 7.31) * 113.0);
    gp += dot(gp, gp + 45.32);
    float grain = (fract(gp.x * gp.y) - 0.5) * P_GRAIN;
    float peak = max(hdr.r, max(hdr.g, hdr.b));
    vec3 back = P_BACKGROUND;
    vec4 color;
#if P_LIGHT_MODE
    vec3 hue = hdr / max(peak, 0.0001);
    float tone = dot(hue, vec3(0.2126, 0.7152, 0.0722));
    vec3 ink = pow(clamp(hue * min(1.0, 0.6 / max(tone, 0.001)), 0.0, 1.0), vec3(1.0 / 2.2));
    float body = smoothstep(0.13, 0.17, source.a);
    vec3 col = mix(ink, vec3(1.0), smoothstep(12.0, 26.0, peak) * P_CORE);
    float glow_a = (1.0 - exp(-peak * 0.5)) * 0.3 * min(P_GLOW, 1.5);
    float alpha = clamp(body * 0.92 + (1.0 - body) * glow_a, 0.0, 1.0) * mask;
    col = mix(ink, col, body);
    color = vec4(clamp(col + grain, 0.0, 1.0) * alpha, alpha);
    // Glass-on-paper look: keep a dark background readable.
    if (dot(back, vec3(0.2126, 0.7152, 0.0722)) < 0.5) back = vec3(0.965, 0.965, 0.97);
#else
    vec3 mapped = 1.0 - exp(-hdr);
    float level = max(mapped.r, max(mapped.g, mapped.b));
    mapped = clamp(mapped + grain * level, 0.0, 1.0) * mask;
    color = vec4(mapped, max(mapped.r, max(mapped.g, mapped.b)));
#endif
    gl_FragColor = vec4(wl_over(back, color), 1.0);
}
