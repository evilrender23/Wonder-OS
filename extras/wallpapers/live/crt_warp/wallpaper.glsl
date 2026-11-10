// CRTWarp — ported from React Bits <CRTWarp /> (see ../LICENSE-REACTBITS.md).
// Single pass: the low-res plasma grid is evaluated per cell on demand, the
// blurred glow texture becomes a Gaussian splat of the neighbouring cells and
// the phosphor persistence (frame feedback) becomes one delayed sample.
// Magnet cursor, channel flip and the warm-up intro are dropped.

const float TAU = 6.28318530718;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec4 seed_for(float channel) {
    if (channel == 0.0) return vec4(0.0);
    float a = sin(channel * 12.9898 + 1.7) * 43758.5453;
    float b = sin(channel * 78.233 + 4.1) * 24634.6345;
    float c = sin(channel * 37.719 + 2.9) * 17431.7731;
    float d = sin(channel * 91.113 + 0.3) * 31337.1337;
    return fract(vec4(a, b, c, d)) * TAU;
}

float field(vec2 uv, float t, vec4 seed) {
    uv = (uv - 0.5) * (max(0.01, P_WAVE_FREQUENCY) / 2.2) + 0.5;
    float f = 0.0;
    f += 0.7 * sin(0.5 * uv.x + t / 5.0 + seed.x);
    f += 3.0 * sin(1.6 * uv.y + t / 5.0 + seed.y);
    f += sin(10.0 * (uv.y * sin(t / 2.0 + seed.z) + uv.x * cos(t / 5.0)) + t / 2.0);
    float cx = uv.x + 0.5 * sin(t / 2.0 + seed.w);
    float cy = uv.y + 0.5 * cos(t / 4.0 + seed.x * 0.7);
    f += 0.4 * sin(sqrt(100.0 * cx * cx + 100.0 * cy * cy + 1.0) + t);
    f += 0.9 * sin(sqrt(75.0 * cx * cx + 25.0 * cy * cy + 1.0) + t);
    f -= 1.4 * sin(sqrt(256.0 * cx * cx + 25.0 * cy * cy + 1.0) + t);
    f += 0.3 * sin(0.5 * uv.y + uv.x + sin(t));
    return f;
}

float level_at(vec2 cell, vec2 grid, float t, vec4 seed) {
    vec2 uv = (cell + 0.5) / grid;
    float levels = float(P_LEVELS);
    float s = 0.5 + 0.499 * sin(field(uv, t, seed) * (P_WAVE_AMPLITUDE / 0.28));
    return floor(s * levels) / max(levels - 1.0, 1.0);
}

// One cell of the content texture (R channel: level with phosphor decay).
float content(vec2 cell, vec2 grid, float t, vec4 seed) {
    cell = clamp(cell, vec2(0.0), grid - 1.0);
    float v = level_at(cell, grid, t, seed);
#if WL_ANIMATED
    if (P_PERSISTENCE > 0.0) {
        // Feedback max(level, previous * decay): the brightest recent past
        // fades out; one delayed sample stands in for the frame history.
        float lag = 0.06 / (1.0 - min(P_PERSISTENCE, 0.95));
        float past = level_at(cell, grid, t - lag * 0.5, seed);
        v = max(v, past * P_PERSISTENCE - 0.004);
    }
#endif
    return v;
}

vec2 curve(vec2 uv, float k) {
    if (k <= 0.0) return uv;
    vec2 p = (uv - 0.5) * 2.0;
    float corner = k / sqrt(max(k * k - 2.0, 0.001));
    p = k * p / sqrt(max(k * k - dot(p, p), 0.001));
    p /= corner;
    return p * 0.5 + 0.5;
}

float pick_mix(vec4 q, float rel) {
    float k = floor(rel);
    float f = smoothstep(0.36, 0.64, rel - k);
    float v;
    if (k < -0.5) v = mix(q.x, q.y, f);
    else if (k < 0.5) v = mix(q.y, q.z, f);
    else v = mix(q.z, q.w, f);
    return pow(v, 1.25);
}

// Four horizontally adjacent cells c0-1 .. c0+2 of one content row.
vec4 row_cells(float c0, float row, vec2 grid, float t, vec4 seed) {
    return vec4(content(vec2(c0 - 1.0, row), grid, t, seed),
                content(vec2(c0, row), grid, t, seed),
                content(vec2(c0 + 1.0, row), grid, t, seed),
                content(vec2(c0 + 2.0, row), grid, t, seed));
}

// beam() of the original for the three beams at once (R and B are shifted).
vec3 beams(vec2 uv, vec2 grid, float lines, float shift, float t, vec4 seed) {
    float ly = uv.y * lines - 0.5;
    float r0 = floor(ly);
    float f = ly - r0;
    float ya = (r0 + 0.5) / lines;
    float yb = (r0 + 1.5) / lines;
    float rowA = clamp(floor(ya * grid.y), 0.0, grid.y - 1.0);
    float rowB = clamp(floor(yb * grid.y), 0.0, grid.y - 1.0);

    float cx = uv.x * grid.x - 0.5;
    float c0 = floor(cx);
    float d = clamp(shift * grid.x, -0.99, 0.99);
    vec3 rel = vec3(cx + d, cx, cx - d) - c0;
    vec3 xs = vec3(uv.x + shift, uv.x, uv.x - shift);
    vec3 inside = step(0.0, xs) * step(xs, vec3(1.0));

    vec4 qa = row_cells(c0, rowA, grid, t, seed);
    vec4 qb = qa;
    if (rowB != rowA) qb = row_cells(c0, rowB, grid, t, seed);

    float insA = step(0.0, ya) * step(ya, 1.0);
    float insB = step(0.0, yb) * step(yb, 1.0);
    vec3 a = vec3(pick_mix(qa, rel.x), pick_mix(qa, rel.y), pick_mix(qa, rel.z)) * inside * insA;
    vec3 b = vec3(pick_mix(qb, rel.x), pick_mix(qb, rel.y), pick_mix(qb, rel.z)) * inside * insB;

    vec3 nearest = f < 0.5 ? a : b;
    vec3 sa = mix(vec3(0.16), vec3(0.34), sqrt(a));
    vec3 sb = mix(vec3(0.16), vec3(0.34), sqrt(b));
    vec3 lit = a * exp(-0.5 * f * f / (sa * sa)) + b * exp(-0.5 * (1.0 - f) * (1.0 - f) / (sb * sb));
    return mix(nearest, lit * 1.18, P_SCANLINE_STRENGTH);
}

// The glow pass: content^2, bilinear upscaled, Gaussian-blurred (sigma =
// height / 48 * radius). Here: a normalised Gaussian splat of 5x3 cells.
float glow_at(vec2 uv, vec2 grid, vec2 res, float t, vec4 seed) {
    vec2 cellPx = res / grid;
    float sigma = max(res.y / 48.0 * P_BLOOM_RADIUS, 1.0);
    vec2 var2 = 2.0 * (vec2(sigma * sigma) + cellPx * cellPx / 6.0);
    vec2 g = clamp(uv, 0.0, 1.0) * grid - 0.5;
    vec2 base = floor(g + 0.5);
    float sum = 0.0;
    float wsum = 0.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -2; i <= 2; i++) {
            vec2 cell = base + vec2(float(i), float(j));
            vec2 dpx = (g - cell) * cellPx;
            float w = exp(-dot(dpx * dpx, 1.0 / var2));
            float v = level_at(clamp(cell, vec2(0.0), grid - 1.0), grid, t, seed);
            sum += v * v * w;
            wsum += w;
        }
    }
    return sum / max(wsum, 1e-4);
}

vec3 mask_at(vec2 px) {
    if (P_MASK == 3 || P_MASK_STRENGTH <= 0.0) return vec3(1.0);
    vec2 q = floor(px);
    float low = 1.0 - P_MASK_STRENGTH;
    vec3 m = vec3(low);
    float column = q.x;
    if (P_MASK == 1) column += mod(floor(q.y / 2.0), 2.0) * 1.5;
    float stripe = mod(floor(column), 3.0);
    if (stripe < 0.5) m.r = 1.0;
    else if (stripe < 1.5) m.g = 1.0;
    else m.b = 1.0;
    if (P_MASK == 1 && mod(q.y, 2.0) > 0.5) m *= mix(1.0, 0.72, P_MASK_STRENGTH);
    if (P_MASK == 2) {
        float slot = mod(q.y + mod(floor(q.x / 3.0), 2.0) * 2.0, 4.0);
        if (slot < 1.0) m *= mix(1.0, 0.45, P_MASK_STRENGTH);
    }
    return m / sqrt((1.0 + 2.0 * low) / 3.0);
}

vec3 phosphor(vec3 cov) {
    float peak = max(max(P_COLOR.r, P_COLOR.g), max(P_COLOR.b, 0.001));
    vec3 weight = 1.0 - P_COLOR / peak;
    return P_COLOR * cov * pow(max(cov, vec3(0.0001)), 0.7 * weight);
}

void main() {
    vec2 res = wl_res();
    vec2 px = wl_frag();
    float clock = wl_time();
    // Plasma clock: speed 0.5 by default.
    float t = clock * 0.5;
    vec4 seed = seed_for(float(P_VARIATION));

    float cols = float(P_COLUMNS);
    float rows = clamp(floor(cols * 0.56 * res.y / max(res.x, 1.0) + 0.5), 4.0, 300.0);
    vec2 grid = vec2(cols, rows);
    float lines = P_SCANLINE_FREQUENCY;

    vec2 frag = px / res;
    float aspect = res.x / res.y;
    float k = P_CURVATURE > 0.001 ? 1.1 + 0.42 / P_CURVATURE : 0.0;
    vec2 uv = curve(frag, k);
    float shape = 1.0;
    if (P_CORNERS > 0.0) {
        float margin = P_CORNERS * 0.035;
        float radius = P_CORNERS * 0.1;
        vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
        vec2 box = (0.5 - margin) * vec2(aspect, 1.0);
        vec2 q = abs(p) - box + radius;
        float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
        float aa = 1.5 / res.y;
        shape = (1.0 - smoothstep(-aa, aa, sd)) * mix(1.0, smoothstep(0.0, 0.06, -sd), 0.35);
        uv = (uv - 0.5) / (1.0 - 2.0 * margin) + 0.5;
    }

    float line = floor(uv.y * lines);
    uv.x += P_INTERFERENCE * 0.003 * (hash(vec2(line * 0.37, floor(clock * 24.0))) - 0.5);

    vec3 level = vec3(0.0);
    float glow = 0.0;
    if (shape > 0.0) {
        level = beams(uv, grid, lines, P_RGB_SHIFT, t, seed);
        if (P_BLOOM > 0.0) glow = glow_at(uv, grid, res, t, seed) * P_BLOOM;
    }

    float hum = 1.0 - P_INTERFERENCE * 0.24 * smoothstep(0.2, 1.0, 0.5 + 0.5 * sin((frag.y - clock * 0.09) * 6.2831853));
    float flick = 1.0 + P_FLICKER * 0.1 * (hash(vec2(floor(clock * 60.0), 7.3)) - 0.5);
    float grain = hash(px + fract(clock) * 173.0) - 0.5;
    vec2 v = frag - 0.5;
    float vignette = mix(1.0, smoothstep(0.0, 1.0, clamp(1.0 - dot(v, v) * 2.0, 0.0, 1.0)), P_VIGNETTE);
    float gain = P_BRIGHTNESS * hum * flick * vignette;

    level *= gain * (1.0 + grain * P_NOISE * 2.0);
    level *= mask_at(px);
    glow *= gain * shape;
    level *= shape;

    vec3 color;
    if (P_LIGHT_MODE == 1) {
        float lumBg = dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722));
        vec3 paper = lumBg > 0.5 ? P_BACKGROUND : vec3(0.96, 0.95, 0.97);
        vec3 cov = clamp(level * 0.86, 0.0, 0.88);
        cov += clamp(glow * 0.12, 0.0, 0.2) * (1.0 - cov);
        color = mix(paper, P_COLOR, cov);
    } else {
        vec3 cov = clamp(level, 0.0, 1.0);
        vec3 light = phosphor(cov) + P_COLOR * glow * 0.5 * (1.0 - cov);
        vec3 excess = max(level - 1.1, 0.0);
        light += (1.0 - light) * (1.0 - exp(-excess * 0.6));
        light = clamp(light, 0.0, 1.0);
        color = P_BACKGROUND + light * (1.0 - P_BACKGROUND);
    }
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
