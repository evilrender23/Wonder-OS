// Radar — ported from React Bits <Radar /> (see ../LICENSE-REACTBITS.md).
// Single pass: the scene shader is kept as is; the CPU-simulated contacts
// become straight chords across the scope (echoes looked up analytically from
// when the sweep last crossed them), and the 6-level bloom chain becomes
// analytic Gaussian halos around the beam, the contacts and the trail.
// Pointer heat, cursor contact and the intro are dropped.

const float TAU = 6.28318531;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float stroke(float distance, float width) {
    float w = max(width, 1.0) * 0.5;
    return clamp(w + 0.5 - distance, 0.0, 1.0) * min(width, 1.0);
}

// Screen pixel (bottom-left origin) -> point on the radar plane; z < 0.5 when
// the ray misses the floor.
vec3 to_plane(vec2 frag, vec2 center, float view_h, float dist, float ct, float st) {
    vec2 s = (frag - center) / view_h;
    vec3 forward = vec3(0.0, st, -ct);
    vec3 up = vec3(0.0, ct, st);
    vec3 dir = forward * 2.0 + vec3(s.x, 0.0, 0.0) + up * s.y;
    vec3 eye = vec3(0.0, -dist * st, dist * ct);
    float hit = -eye.z / min(dir.z, -0.00001);
    return vec3(eye.xy + dir.xy * hit, dir.z > -0.0001 ? 0.0 : 1.0);
}

// Gaussian halos at the bloom mip radii (3..96 px), for a line of width w.
float line_bloom(float d, float w) {
    float b = 0.0;
    float sigma = 3.0;
    for (int l = 0; l < 6; l++) {
        b += w / (sigma * 2.5066) * exp(-d * d / (2.0 * sigma * sigma));
        sigma *= 2.0;
    }
    return b;
}

float dot_bloom(float d, float energy) {
    float b = 0.0;
    float sigma = 3.0;
    for (int l = 0; l < 6; l++) {
        b += energy / (6.2832 * sigma * sigma) * exp(-d * d / (2.0 * sigma * sigma));
        sigma *= 2.0;
    }
    return b;
}

// Contact i on chord n: start point, unit direction, chord length.
void chord(float i, float n, out vec2 a, out vec2 dir, out float len) {
    vec2 k = vec2(i * 7.31 + 1.7, n * 3.17);
    float angle = hash(k) * TAU;
    float dev = (hash(k + 11.3) - 0.5) * 1.6;
    a = vec2(sin(angle), cos(angle)) * 0.94;
    float heading = angle + 3.14159265 + dev;
    dir = vec2(sin(heading), cos(heading));
    len = 2.0 * 0.94 * cos(dev);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float view_h = res.y;
    vec2 center = vec2(P_CENTER_X * res.x, (1.0 - P_CENTER_Y) * res.y);
    float fit = P_SCALE * min(res.x, res.y) / res.y;
    float dist = 4.0 / fit;
    float tilt = radians(P_TILT);
    float ct = cos(tilt);
    float st = sin(tilt);
    float rings_n = float(P_RING_COUNT);
    float spokes_n = float(P_SPOKE_COUNT);
    float line_w = P_LINE_WIDTH;
    float ticks_on = P_TICKS == 1 ? 1.0 : 0.0;
    float unit = 2.0 / (fit * view_h);
    vec3 accent = pow(P_COLOR, vec3(2.2));

    float T = wl_time();
    bool pulse_mode = P_MODE == 1;
    float period = pulse_mode ? 3.2 : 6.0;
    float span = pulse_mode ? 1.15 : TAU;
    float direction = (!pulse_mode && P_COUNTERCLOCKWISE == 1) ? -1.0 : 1.0;
    float sweep_turns = T / period;
    float cycle = floor(sweep_turns);
    float sweep = fract(sweep_turns) * span;
    if (direction < 0.0) sweep = span - sweep;
    float trail_k = pulse_mode ? P_TRAIL * 0.22 : P_TRAIL * TAU * 0.5;

    vec3 light = vec3(0.0);
    vec3 bloom = vec3(0.0);

    vec3 hit = to_plane(frag, center, view_h, dist, ct, st);
    vec2 p = hit.xy;
    float r = length(p);
    if (hit.z > 0.5 && r < 1.3) {
        vec2 dpdx = to_plane(frag + vec2(1.0, 0.0), center, view_h, dist, ct, st).xy - p;
        vec2 dpdy = to_plane(frag + vec2(0.0, 1.0), center, view_h, dist, ct, st).xy - p;
        vec2 radial = p / max(r, 0.00001);
        vec2 tangent = vec2(radial.y, -radial.x);
        float pxR = max(length(vec2(dot(radial, dpdx), dot(radial, dpdy))), 0.000001);
        float pxT = max(length(vec2(dot(tangent, dpdx), dot(tangent, dpdy))), 0.000001);
        float px_plane = sqrt(pxR * pxT);
        float bearing = atan(p.x, p.y);
        float turn = fract(bearing / TAU + 1.0);
        float inside = 1.0 - smoothstep(1.0 - pxR, 1.0 + pxR, r);

        float ring_coord = r * rings_n;
        float ring_index = clamp(floor(ring_coord + 0.5), 1.0, max(rings_n - 1.0, 1.0));
        float rings = stroke(abs(ring_coord - ring_index) / rings_n / pxR, line_w) * step(ring_index, rings_n - 0.5);
        float outer = stroke(abs(r - 1.0) / pxR, line_w * 1.15);

        float tick_coord = turn * 180.0;
        float tick_index = floor(tick_coord + 0.5);
        float major = 1.0 - step(0.5, mod(tick_index, 15.0));
        float middle = 1.0 - step(0.5, mod(tick_index, 5.0));
        float tick_len = mix(mix(0.016, 0.03, middle), 0.05, major);
        float tick_radial = clamp((r - 1.0) / pxR + 0.5, 0.0, 1.0) * clamp((1.0 + tick_len - r) / pxR + 0.5, 0.0, 1.0);
        float tick_spacing = TAU / 180.0 * r / pxT;
        float ticks = stroke(abs(tick_coord - tick_index) / 180.0 * TAU * r / pxT, line_w) * tick_radial;
        ticks *= mix(mix(0.42 * smoothstep(2.5, 5.0, tick_spacing), 0.7, middle), 1.0, major) * ticks_on;

        float spoke_total = max(spokes_n, 1.0);
        float spoke_coord = turn * spoke_total;
        float spoke_index = floor(spoke_coord + 0.5);
        float spokes = stroke(abs(spoke_coord - spoke_index) / spoke_total * TAU * r / pxT, line_w) * smoothstep(0.05, 0.16, r);
        spokes *= inside * step(0.5, spokes_n);
        float cardinal_coord = turn * 4.0;
        float axis_distance = abs(cardinal_coord - floor(cardinal_coord + 0.5)) / 4.0 * TAU * r / pxT;
        float cardinal = stroke(axis_distance, line_w) * smoothstep(0.02, 0.08, r) * inside;
        float hub = stroke(abs(r - 0.022) / pxR, line_w);
        float range_coord = r * rings_n * 5.0;
        float range_mark = stroke(abs(range_coord - floor(range_coord + 0.5)) / (rings_n * 5.0) / pxR, line_w);
        range_mark *= 1.0 - smoothstep(2.5, 3.5, axis_distance);
        range_mark *= smoothstep(0.06, 0.12, r) * inside;
        float bezel = stroke(abs(r - 1.065) / pxR, line_w) * ticks_on;

        float grid = rings * 0.12 + outer * 0.24 + ticks * 0.22 + bezel * 0.08;
        grid += spokes * 0.05 + cardinal * 0.08 + hub * 0.22 + range_mark * 0.16;
        grid *= P_GRID_OPACITY;

        float behind;
        float edge;
        float trail;
        float last_pass;
        float front_px;   // signed pixel distance to the beam, > 0 on the trail side
        if (!pulse_mode) {
            float ahead = fract((bearing - sweep) / TAU + 0.5) - 0.5;
            behind = fract(direction * (sweep - bearing) / TAU) * TAU;
            edge = stroke(abs(ahead) * TAU * r / pxT, line_w * 1.4) * smoothstep(0.0, 0.04, r) * inside;
            trail = exp(-behind / trail_k);
            last_pass = floor(cycle - behind / TAU);
            front_px = -direction * ahead * TAU * r / pxT;
        } else {
            behind = sweep - r;
            edge = stroke(abs(behind) / pxR, line_w * 1.4) * inside;
            trail = behind >= 0.0 ? exp(-behind / trail_k) : 0.0;
            last_pass = behind >= 0.0 ? cycle : cycle - 1.0;
            front_px = behind / pxR;
        }
        trail *= inside;
        float glow_trail = pow(trail, 0.45);

        float returns = 0.0;
        float returns_avg = 0.0;
        if (P_CLUTTER > 0.0 && trail > 0.004) {
            float radial_bin = floor(r * 120.0);
            float count = max(8.0, floor(radial_bin * 5.0));
            float angular_bin = floor(turn * count);
            vec2 drift = vec2(last_pass * 0.173, last_pass * 0.291);
            float weather = noise(p * 3.2 + drift) * 0.65 + noise(p * 7.1 - drift) * 0.35;
            float cluster = smoothstep(0.5, 0.78, weather);
            float density = P_CLUTTER * (0.02 + 0.35 * cluster) * (1.0 - smoothstep(0.55, 1.0, r));
            vec2 bin_seed = vec2(radial_bin, angular_bin) + mod(last_pass, 512.0) * 17.13;
            float speck = step(1.0 - density, hash(bin_seed));
            vec2 in_bin = vec2(fract(r * 120.0), fract(turn * count)) - 0.5;
            float shape = 1.0 - smoothstep(0.18, 0.5, length(in_bin));
            speck *= shape * (0.45 + 0.55 * hash(bin_seed + 7.7));
            returns = (speck * 1.1 + cluster * 0.16 * P_CLUTTER) * trail;
            returns_avg = (density * 0.22 * 1.1 + cluster * 0.16 * P_CLUTTER) * trail;
        }

        vec3 line_color = mix(vec3(1.0), accent, clamp(glow_trail * 0.9, 0.0, 1.0));
        light = line_color * grid * (1.0 + glow_trail * 3.0);
        float fill_shape = 0.11 * (0.35 + 0.65 * smoothstep(0.0, 0.6, r));
        float fill = trail * fill_shape;
        light += accent * (fill + returns * 0.55);
        vec3 beam = mix(accent, vec3(1.0), 0.3);
        light += beam * edge * 1.3;

        // Bloom: the mip chain adds six ever wider blurs of the scene. The
        // smooth trail keeps its value (with its hard front blurred per
        // level), thin lines become Gaussian halos.
        float soft_inside = 1.0 - smoothstep(1.0 - 24.0 * pxR, 1.0 + 24.0 * pxR, r);
        float front_step = front_px > 0.0 ? 1.0 : 0.0;
        float trail_blur = 0.0;
        float sigma = 3.0;
        for (int l = 0; l < 6; l++) {
            float phi = 0.5 + 0.5 * wl_tanh(front_px / sigma * 0.8);
            trail_blur += max(trail + phi - front_step, 0.0);
            sigma *= 2.0;
        }
        trail_blur *= soft_inside;
        bloom += accent * (trail_blur * fill_shape + 6.0 * returns_avg * 0.55);
        float ring_px = abs(ring_coord - ring_index) / rings_n / pxR;
        float grid_halo = 0.12 * line_bloom(ring_px, line_w) * step(ring_index, rings_n - 0.5)
                        + 0.24 * line_bloom(abs(r - 1.0) / pxR, line_w * 1.15);
        bloom += line_color * grid_halo * P_GRID_OPACITY * (1.0 + glow_trail * 3.0);
        float beam_px = pulse_mode ? abs(behind) / pxR : abs(front_px);
        bloom += beam * 1.3 * line_bloom(beam_px, line_w * 1.4) * soft_inside * smoothstep(0.0, 0.04, r);

        // Contacts: straight chords across the scope; each sweep pass leaves an
        // echo where the contact was, the newest one with a ping ring.
        float persist = period * 1.1;
        for (int ti = 0; ti < 16; ti++) {
            if (ti >= P_TARGETS) break;
            float fi = float(ti);
            float speed_c = 0.012 + hash(vec2(fi * 3.3 + 0.5, 9.1)) * 0.03;
            float duration = 1.7 / speed_c;
            float clock = T / duration + hash(vec2(fi * 5.1 + 2.0, 4.4));
            vec2 a0; vec2 d0; float l0;
            chord(fi, floor(clock), a0, d0, l0);
            vec2 now = a0 + d0 * l0 * fract(clock);
            // Time since the sweep last crossed the contact.
            float age0;
            if (!pulse_mode) {
                float b = atan(now.x, now.y);
                age0 = fract(direction * (sweep - b) / TAU) * period;
            } else {
                float rr = length(now);
                age0 = (sweep >= rr ? sweep - rr : sweep + span - rr) / span * period;
            }
            for (int k = 0; k < 4; k++) {
                float age = age0 + float(k) * period;
                float fade = exp(-age / persist) * (k == 0 ? 1.0 : 0.8);
                float back_clock = clock - age / duration;
                float nb = floor(back_clock);
                vec2 a1; vec2 d1; float l1;
                chord(fi, nb, a1, d1, l1);
                vec2 e = a1 + d1 * l1 * fract(back_clock);
                vec2 off = p - e;
                float d_px = length(off) / px_plane;
                if (d_px > 180.0 || fade < 0.01) continue;
                float z = unit * (k == 0 ? 3.4 : 2.4);
                float ang = -atan(e.x, e.y);
                float c = cos(ang);
                float s = sin(ang);
                vec2 local = vec2(c * off.x + s * off.y, -s * off.x + c * off.y) / z;
                float dd = length(local * vec2(0.62, 1.0));
                float aa = max(px_plane / z, 0.0001);
                float shape = clamp((1.0 - dd) / aa + 0.5, 0.0, 1.0);
                shape *= step(abs(local.x), 1.25) * step(abs(local.y), 1.25);
                shape *= 0.5 + 0.5 * exp(-dd * dd * 2.5);
                float core = k == 0 ? 1.0 : 0.3;
                vec3 tint = mix(accent, vec3(1.0), core * 0.45 * exp(-dd * dd * 5.0));
                float strength = fade * 2.2;
                light += tint * shape * strength;
                float z_px = z / px_plane;
                bloom += accent * dot_bloom(d_px, strength * 2.4 * z_px * z_px);
                if (k == 0 && age0 < 1.4) {
                    float prog = age0 / 1.4;
                    float eased = 1.0 - pow(1.0 - prog, 3.0);
                    float rad = unit * (4.0 + 26.0 * eased);
                    float rd = length(off) / rad;
                    float raa = max(px_plane / rad, 0.0001);
                    float ring = clamp(1.0 - abs(rd - 1.0) / (raa * 0.75), 0.0, 1.0);
                    light += mix(accent, vec3(1.0), 0.2) * ring * (1.0 - prog) * (1.0 - prog) * 1.4;
                }
            }
        }
    }

    vec3 hdr = light * 1.5 * P_BRIGHTNESS;
    vec3 glow = bloom * 1.5 * P_BRIGHTNESS * P_GLOW * 0.5;
    vec2 uv = frag / res;
    float edge_f = P_FADE * 0.5;
    float mask = wl_smoother(uv.x / max(edge_f, 0.0001)) * wl_smoother((1.0 - uv.x) / max(edge_f, 0.0001))
               * wl_smoother(uv.y / max(edge_f, 0.0001)) * wl_smoother((1.0 - uv.y) / max(edge_f, 0.0001));
    float grain = (hash(frag + fract(T * 7.31) * 113.0) - 0.5) * P_GRAIN;
    vec3 back = P_BACKGROUND;
    vec4 color;
#if P_LIGHT_MODE
    float bloom_peak = max(glow.r, max(glow.g, glow.b));
    float bloom_low = min(glow.r, min(glow.g, glow.b));
    vec3 total = hdr + glow * 0.3 * (bloom_peak - bloom_low) / max(bloom_peak, 0.0001);
    float peak = max(total.r, max(total.g, total.b));
    vec3 hue = total / max(peak, 0.0001);
    float tone = dot(hue, vec3(0.2126, 0.7152, 0.0722));
    float depth = mix(0.36, 0.09, smoothstep(0.06, 0.7, peak));
    vec3 ink = pow(clamp(hue * min(1.0, depth / max(tone, 0.001)), 0.0, 1.0), vec3(1.0 / 2.2));
    float alpha = clamp((1.0 - exp(-peak * 2.6)) * (1.0 + grain), 0.0, 1.0) * mask;
    color = vec4(ink * alpha, alpha);
    // Ink is meant for light pages: keep a dark background readable.
    if (dot(back, vec3(0.2126, 0.7152, 0.0722)) < 0.5) back = vec3(0.965, 0.965, 0.97);
#else
    vec3 mapped = 1.0 - exp(-(hdr + glow));
    float level = max(mapped.r, max(mapped.g, mapped.b));
    mapped = clamp(mapped + grain * level, 0.0, 1.0) * mask;
    color = vec4(mapped, max(mapped.r, max(mapped.g, mapped.b)));
#endif
    gl_FragColor = vec4(wl_over(back, color), 1.0);
}
