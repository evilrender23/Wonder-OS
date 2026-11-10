// SoftAurora — ported from React Bits <SoftAurora /> (see ../LICENSE-REACTBITS.md).
// Already single pass; cursor drift/reach, click pulses and the intro bloom
// are dropped. The premultiplied output is composited over a background.

#define TAU 6.28318

vec3 gradient_hash(vec3 p) {
    p = vec3(
        dot(p, vec3(127.1, 311.7, 234.6)),
        dot(p, vec3(269.5, 183.3, 198.3)),
        dot(p, vec3(169.5, 283.3, 156.9))
    );
    vec3 h = fract(sin(p) * 43758.5453123);
    float phi = acos(2.0 * h.x - 1.0);
    float theta = TAU * h.y;
    return vec3(cos(theta) * sin(phi), sin(theta) * cos(phi), cos(phi));
}

float quintic(float t) {
    float t2 = t * t;
    float t3 = t * t2;
    return 6.0 * t3 * t2 - 15.0 * t2 * t2 + 10.0 * t3;
}

vec3 cosine_gradient(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(TAU * (c * t + d));
}

float perlin3(float amplitude, float frequency, float px, float py, float pz) {
    float x = px * frequency;
    float y = py * frequency;
    float fx = floor(x);
    float fy = floor(y);
    float fz = floor(pz);
    float cx = ceil(x);
    float cy = ceil(y);
    float cz = ceil(pz);

    float d000 = dot(gradient_hash(vec3(fx, fy, fz)), vec3(x - fx, y - fy, pz - fz));
    float d100 = dot(gradient_hash(vec3(cx, fy, fz)), vec3(x - cx, y - fy, pz - fz));
    float d010 = dot(gradient_hash(vec3(fx, cy, fz)), vec3(x - fx, y - cy, pz - fz));
    float d110 = dot(gradient_hash(vec3(cx, cy, fz)), vec3(x - cx, y - cy, pz - fz));
    float d001 = dot(gradient_hash(vec3(fx, fy, cz)), vec3(x - fx, y - fy, pz - cz));
    float d101 = dot(gradient_hash(vec3(cx, fy, cz)), vec3(x - cx, y - fy, pz - cz));
    float d011 = dot(gradient_hash(vec3(fx, cy, cz)), vec3(x - fx, y - cy, pz - cz));
    float d111 = dot(gradient_hash(vec3(cx, cy, cz)), vec3(x - cx, y - cy, pz - cz));

    float sx = quintic(x - fx);
    float sy = quintic(y - fy);
    float sz = quintic(pz - fz);
    float ly0 = mix(mix(d000, d100, sx), mix(d010, d110, sx), sy);
    float ly1 = mix(mix(d001, d101, sx), mix(d011, d111, sx), sy);
    return amplitude * mix(ly0, ly1, sz);
}

float aurora_glow(float t, vec2 uv) {
    float noise_val = 0.0;
    float freq = P_NOISE_FREQUENCY;
    float amp = P_NOISE_AMPLITUDE;
    vec2 sample_pos = uv * P_SCALE;
    for (int i = 0; i < 3; i++) {
        noise_val += perlin3(amp, freq, sample_pos.x, sample_pos.y, t);
        amp *= P_OCTAVE_DECAY;
        freq *= 2.0;
    }
    float y_band = uv.y * 10.0 - P_BAND_HEIGHT * 10.0;
    return 0.3 * max(exp(P_BAND_SPREAD * (1.0 - 1.1 * abs(noise_val + y_band))), 0.0);
}

vec2 turn(vec2 p, vec2 center) {
    float a = radians(P_TILT);
    float c = cos(a);
    float s = sin(a);
    vec2 d = p - center;
    return center + vec2(c * d.x - s * d.y, s * d.x + c * d.y);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    vec2 uv = frag / res;
    vec2 center = vec2(0.5 * res.x / res.y, 0.5);
    vec2 field = turn(frag / res.y, center);
    // flow += dt * speed (default 0.6), t = 0.4 * flow;
    // hue += dt * speed * colorSpeed.
    float flow = wl_time() * 0.6;
    float t = 0.4 * flow;
    float hue = flow * P_COLOR_SPEED;

    float glow1 = aurora_glow(t, field);
    // Both layers share the same noise unless layer_offset separates them.
    float glow2 = glow1;
    if (P_LAYER_OFFSET != 0.0) glow2 = aurora_glow(t + P_LAYER_OFFSET, field);
    vec3 gradient1 = mix(vec3(1.0), cosine_gradient(uv.x + hue * 0.2, vec3(0.5), vec3(0.5), vec3(1.0), vec3(0.3, 0.20, 0.20)), P_SPECTRUM);
    vec3 gradient2 = mix(vec3(1.0), cosine_gradient(uv.x + hue * 0.1, vec3(0.5), vec3(0.5), vec3(2.0, 1.0, 0.0), vec3(0.5, 0.20, 0.25)), P_SPECTRUM);

    vec3 col = 0.99 * glow1 * gradient1 * P_COLOR1;
    col += 0.99 * glow2 * gradient2 * P_COLOR2;
    col *= P_BRIGHTNESS;

    float grain = (fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;
    vec4 premul;
    vec3 page;
#if P_LIGHT_MODE
    float energy = max(col.r, max(col.g, col.b));
    vec3 hue_c = clamp(col / max(energy, 1e-4), 0.0, 1.0);
    vec3 average = (glow1 * P_COLOR1 + glow2 * P_COLOR2) / max(glow1 + glow2, 1e-4);
    float top = max(average.r, max(average.g, average.b));
    float spread = top - min(average.r, min(average.g, average.b));
    float core = smoothstep(0.55, 1.6, energy);
    float depth = 1.25 + 0.45 * core;
    vec3 tint = mix(average * 0.82, pow(average / max(top, 1e-4), vec3(depth + 0.6)), smoothstep(0.04, 0.2, spread));
    float vivid = smoothstep(0.12, 0.55, 1.0 - min(hue_c.r, min(hue_c.g, hue_c.b))) * smoothstep(0.04, 0.2, spread);
    vec3 ink = mix(tint, pow(hue_c, vec3(depth)), vivid) * mix(0.93, 0.8, core);
    float density = 0.88 * (1.0 - exp(-1.9 * pow(energy, 1.7)));
    density = clamp(density + grain, 0.0, 1.0);
    premul = vec4(ink * density, density);
    page = P_LIGHT_BACKGROUND;
#else
    float alpha = clamp(length(col), 0.0, 1.0);
    vec3 color = clamp(col, 0.0, 1.0);
    premul = vec4(clamp(color * alpha + grain, 0.0, alpha), alpha);
    page = P_BACKGROUND;
#endif
    gl_FragColor = vec4(wl_over(page, premul), 1.0);
}
