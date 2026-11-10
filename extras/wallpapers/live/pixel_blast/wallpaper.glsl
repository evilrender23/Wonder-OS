// PixelBlast — ported from React Bits <PixelBlast /> (see ../LICENSE-REACTBITS.md).
// Single pass: the cell pass (value-noise fbm per 8x8-pixel cell, stored in a
// bilinear texture) is evaluated directly at the pixel's continuous cell
// position, which is what the linear filtering reconstructed. Hover trail,
// click ripples and the intro blast are dropped; auto ripples are kept as
// hashed, periodic spawns. The tilt plane is applied analytically.

const float PI = 3.14159265;

float hash11(float n) { return fract(sin(n) * 43758.5453); }

float vnoise(vec3 p) {
    vec3 ip = floor(p);
    vec3 fp = fract(p);
    float n = dot(ip, vec3(1.0, 57.0, 113.0));
    float n000 = hash11(n);
    float n100 = hash11(n + 1.0);
    float n010 = hash11(n + 57.0);
    float n110 = hash11(n + 58.0);
    float n001 = hash11(n + 113.0);
    float n101 = hash11(n + 114.0);
    float n011 = hash11(n + 170.0);
    float n111 = hash11(n + 171.0);
    vec3 w = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);
    float x00 = mix(n000, n100, w.x);
    float x10 = mix(n010, n110, w.x);
    float x01 = mix(n001, n101, w.x);
    float x11 = mix(n011, n111, w.x);
    return mix(mix(x00, x10, w.y), mix(x01, x11, w.y), w.z) * 2.0 - 1.0;
}

float fbm2(vec2 uv, float t) {
    vec3 p = vec3(uv * P_PATTERN_SCALE, t);
    float freq = 1.0;
    float sum = 1.0;
    for (int i = 0; i < 5; i++) {
        sum += vnoise(p * freq);
        freq *= 1.25;
    }
    return sum * 0.5 + 0.5;
}

// Cell feed at a (continuous) cell coordinate, centred pixels.
float cell_feed(vec2 cell_coord, vec2 res, float tp, vec2 drift, float clock) {
    float aspect = res.x / res.y;
    vec2 uv = cell_coord / res * vec2(aspect, 1.0);
    float base = fbm2(uv + drift, tp * 0.05);
    base = base * 0.5 - 0.65;
    float feed = base + (P_PATTERN_DENSITY - 0.5) * 0.3;
    if (P_AUTO_RIPPLE > 0.0) {
    // Auto ripples: one spawn per period at a hashed spot, the last few live.
    float period = P_AUTO_RIPPLE;
    float cps = 8.0 * P_PIXEL_SIZE;
    float now_slot = floor(clock / period);
    for (int k = 0; k < 4; k++) {
        float slot = now_slot - float(k);
        if (slot < 0.0) break;
        vec2 rnd = wl_hash22(vec2(slot, P_VARIATION) + 0.37);
        float jitter = wl_hash12(vec2(slot, 7.0 + P_VARIATION));
        float born = (slot + 0.3 * jitter) * period;
        float age = (clock - born) * 0.5;
        if (age < 0.0) continue;
        vec2 pos = (0.15 + rnd * 0.7) * res;
        vec2 cuv = ((pos - res * 0.5 - cps * 0.5) / res) * vec2(aspect, 1.0);
        float r = distance(uv, cuv);
        float wave_r = P_RIPPLE_SPEED * age;
        float ring = exp(-pow((r - wave_r) / P_RIPPLE_THICKNESS, 2.0));
        float atten = exp(-1.0 * age) * exp(-10.0 * r);
        feed = max(feed, ring * atten * P_RIPPLE_INTENSITY);
    }
    }
    return feed;
}

float bayer2(vec2 v) {
    vec2 f = floor(v);
    return fract(f.x / 2.0 + f.y * f.y * 0.75);
}

float bayer8(vec2 v) {
    return (bayer2(v / 4.0) * 0.25 + bayer2(v / 2.0)) * 0.25 + bayer2(v);
}

float aa_from(vec2 grad, vec2 dx, vec2 dy) {
    return abs(dot(grad, dx)) + abs(dot(grad, dy));
}

float mask_square(vec2 p, float cov, float soft, vec2 dx, vec2 dy) {
    if (soft <= 0.0) return cov;
    float side = 0.5 * sqrt(clamp(cov, 0.0, 1.0));
    vec2 c = p - 0.5;
    vec2 q = abs(c) - side;
    float d = max(q.x, q.y);
    vec2 grad = q.x > q.y ? vec2(sign(c.x), 0.0) : vec2(0.0, sign(c.y));
    float aa = max(aa_from(grad, dx, dy), 1e-4);
    return 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);
}

float mask_circle(vec2 p, float cov, vec2 dx, vec2 dy) {
    float r = sqrt(cov) * 0.25;
    vec2 c = p - 0.5;
    float len = length(c);
    float d = len - r;
    float aa = 0.5 * aa_from(c / max(len, 1e-5), dx, dy);
    return cov * (1.0 - smoothstep(-aa, aa, d * 2.0));
}

float mask_triangle(vec2 p, vec2 id, float cov, vec2 dx, vec2 dy) {
    bool flip = mod(id.x + id.y, 2.0) > 0.5;
    if (flip) p.x = 1.0 - p.x;
    float r = sqrt(cov);
    float d = p.y - r * (1.0 - p.x);
    float aa = max(aa_from(vec2(flip ? -r : r, 1.0), dx, dy), 1e-4);
    return cov * clamp(0.5 - d / aa, 0.0, 1.0);
}

float mask_diamond(vec2 p, float cov) {
    float r = sqrt(cov) * 0.564;
    return step(abs(p.x - 0.49) + abs(p.y - 0.49), r);
}

float mask_cross(vec2 p, float cov, vec2 dx, vec2 dy) {
    float s = sqrt(clamp(cov, 0.0, 1.0));
    vec2 c = p - 0.5;
    vec2 q = abs(c);
    float arm = 0.42 * s;
    float bar = 0.13 * s + 0.02;
    float a1 = max(q.x - arm, q.y - bar);
    float a2 = max(q.x - bar, q.y - arm);
    float d = min(a1, a2);
    bool horizontal = a1 < a2 ? q.x - arm > q.y - bar : q.x - bar > q.y - arm;
    vec2 grad = horizontal ? vec2(sign(c.x), 0.0) : vec2(0.0, sign(c.y));
    float aa = max(aa_from(grad, dx, dy), 1e-4);
    return 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);
}

float mask_ring(vec2 p, float cov, vec2 dx, vec2 dy) {
    float s = sqrt(clamp(cov, 0.0, 1.0));
    vec2 c = p - 0.5;
    float len = length(c);
    float d = abs(len - 0.3 * s) - 0.09 * s;
    vec2 grad = sign(len - 0.3 * s) * c / max(len, 1e-5);
    float aa = max(aa_from(grad, dx, dy), 1e-4);
    return 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);
}

// Inverse of the component's perspective plane (lower-triangular tilt
// conjugated by a translation to the centre), applied to a top-left point.
vec3 plane_of(vec2 p, vec2 res) {
    float ax = radians(P_TILT_X);
    float ay = radians(P_TILT_Y);
    float depth = max(res.x, res.y) * 1.25;
    float a = cos(ay);
    float b = sin(ax) * sin(ay);
    float c = cos(ax);
    float d = sin(ay) * cos(ax) / depth;
    float e = -sin(ax) / depth;
    vec2 cen = res * 0.5;
    vec3 u = vec3(p - cen, 1.0);
    vec3 w = vec3(u.x / a, -b / (a * c) * u.x + u.y / c, (b * e - c * d) / (a * c) * u.x - e / c * u.y + u.z);
    return vec3(w.xy + cen * w.z, w.z);
}

vec2 plane_screen(vec2 screen, vec2 res, out float z) {
    vec3 pl = plane_of(vec2(screen.x, res.y - screen.y), res);
    z = pl.z;
    return vec2(pl.x, res.y * pl.z - pl.y) / max(pl.z, 1e-4);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float ps = P_PIXEL_SIZE;
    float clock = wl_time();
    // patternTime = variation * 37 + elapsed * speed (default 0.5).
    float tp = P_VARIATION * 37.0 + clock * 0.5;
    float drift_a = radians(P_DRIFT_ANGLE);
    vec2 drift = vec2(cos(drift_a), sin(drift_a)) * P_DRIFT * 0.05 * clock;

#if P_LIGHT_MODE
    vec3 page = P_LIGHT_BACKGROUND;
#else
    vec3 page = P_BACKGROUND;
#endif

    vec2 screen = frag;
    float lod = 0.0;
    float depth_fade = 1.0;
    vec2 gdx = vec2(1.0 / ps, 0.0);
    vec2 gdy = vec2(0.0, 1.0 / ps);
    if (P_TILT_X != 0.0 || P_TILT_Y != 0.0) {
    float z;
    float z1;
    screen = plane_screen(frag, res, z);
    vec2 sx = plane_screen(frag + vec2(1.0, 0.0), res, z1);
    vec2 sy = plane_screen(frag + vec2(0.0, 1.0), res, z1);
    gdx = (sx - screen) / ps;
    gdy = (sy - screen) / ps;
    float footprint = length(abs(sx - screen) + abs(sy - screen));
    lod = smoothstep(0.3, 0.85, footprint / ps);
    depth_fade = (1.0 - smoothstep(0.45, 1.0, footprint / (8.0 * ps))) * step(0.0, z);
    if (depth_fade <= 0.0) {
        gl_FragColor = vec4(page, 1.0);
        return;
    }
    }

    vec2 frag_coord = screen - res * 0.5;
    float cps = 8.0 * ps;
    // Bilinear cell texture: texel i holds the value at cell i, centred at
    // (i + 0.5) cells, so the smooth field is the cell function half a cell back.
    float smooth_raw = cell_feed(frag_coord - 0.5 * cps, res, tp, drift, clock);
    float smooth_feed = clamp((smooth_raw + 0.5) * 0.5, 0.0, 1.0) * 2.0 - 0.5;
    float feed = smooth_feed;
    if (P_SMOOTHING < 1.0) {
        vec2 cell_id = floor(frag_coord / cps);
        float cell = clamp(cell_feed(cell_id * cps, res, tp, drift, clock), -6.0, 10.0);
        feed = mix(cell, smooth_feed, P_SMOOTHING);
    }

    if (feed < -0.12 * P_SOFTNESS && smooth_feed < 0.2 && (lod <= 0.0 || smooth_feed <= 0.0)) {
        gl_FragColor = vec4(page, 1.0);
        return;
    }

    vec2 pixel_id = floor(frag_coord / ps);
    vec2 pixel_uv = frag_coord / ps - pixel_id;
    float bayer = bayer8(mod(pixel_id, 8.0)) - 0.5;
    float margin = feed + bayer - 0.5;
    float bw = P_SOFTNESS > 0.0 ? smoothstep(-0.12 * P_SOFTNESS, 0.12 * P_SOFTNESS, margin) : step(0.0, margin);

    float h = (P_PIXEL_SIZE_JITTER > 0.0 || P_TWINKLE > 0.0) ? fract(sin(dot(pixel_id, vec2(127.1, 311.7))) * 43758.5453) : 0.5;
    float jitter_scale = 1.0 + (h - 0.5) * P_PIXEL_SIZE_JITTER;
    float heat = clamp((feed - 1.0) * 2.5, 0.0, 1.0) * min(P_GLOW * 2.0, 1.0);
    float coverage = bw * jitter_scale * (1.0 + heat * 0.35);
    float M;
    float area;
#if P_VARIANT == 1
    M = mask_circle(pixel_uv, coverage, gdx, gdy);
    area = 0.196;
#elif P_VARIANT == 2
    M = mask_triangle(pixel_uv, pixel_id, coverage, gdx, gdy);
    area = 0.5;
#elif P_VARIANT == 3
    M = mask_diamond(pixel_uv, coverage);
    area = 0.62;
#elif P_VARIANT == 4
    M = mask_cross(pixel_uv, coverage, gdx, gdy);
    area = 0.41;
#elif P_VARIANT == 5
    M = mask_ring(pixel_uv, coverage, gdx, gdy);
    area = 0.41;
#else
    M = mask_square(pixel_uv, coverage, P_SOFTNESS, gdx, gdy);
    area = 1.0;
#endif
    if (lod > 0.0) {
        float average = clamp(smooth_feed, 0.0, 1.0) * area * (1.0 + heat * 0.35);
        M = mix(M, average, lod);
    }

    if (P_TWINKLE > 0.0) {
        float rate = 1.5 + 3.0 * fract(h * 7.31);
        float wave = 0.5 + 0.5 * sin(clock * rate + h * 37.0);
        M *= 1.0 - P_TWINKLE * 0.55 * wave * wave;
    }

    float density = clamp(smooth_feed, 0.0, 1.0);
    float glow = 0.0;
    if (P_GLOW > 0.0) {
        float bloom = smoothstep(0.2, 1.0, density);
#if P_LIGHT_MODE
        glow = bloom * bloom * P_GLOW * 0.12 * depth_fade;
#else
        glow = bloom * bloom * P_GLOW * 0.22 * depth_fade;
#endif
    }

    float fade = depth_fade;
    if (P_EDGE_FADE > 0.0) {
        vec2 norm = frag / res;
        vec2 ea = clamp(norm / P_EDGE_FADE, 0.0, 1.0);
        vec2 eb = clamp((1.0 - norm) / P_EDGE_FADE, 0.0, 1.0);
        vec2 sa = ea * ea * ea * (ea * (ea * 6.0 - 15.0) + 10.0);
        vec2 sb = eb * eb * eb * (eb * (eb * 6.0 - 15.0) + 10.0);
        fade *= sa.x * sa.y * sb.x * sb.y;
    }
    M = clamp(M, 0.0, 1.0) * fade;
    glow *= fade;

    float hot = clamp(heat, 0.0, 1.0);
    float tone = smoothstep(0.3, 1.0, density) * 0.85;
    vec3 color = mix(P_COLOR, P_ACCENT_COLOR, max(tone, hot));
#if P_LIGHT_MODE
    vec3 lit = color * (1.0 - 0.35 * hot);
    vec3 haze = mix(color, vec3(1.0), 0.35);
#else
    vec3 lit = mix(color, vec3(1.0), 0.45 * hot);
    vec3 haze = color;
#endif
    if (P_NOISE_AMOUNT > 0.0) {
        float n = fract(sin(dot(floor(frag) + floor(tp * 60.0), vec2(127.1, 311.7))) * 43758.5453);
        lit = clamp(lit + (n - 0.5) * P_NOISE_AMOUNT, 0.0, 1.0);
    }

    float alpha = M + glow * (1.0 - M);
    vec3 premul = lit * M + haze * glow * (1.0 - M);
    gl_FragColor = vec4(wl_over(page, vec4(premul, alpha)), 1.0);
}
