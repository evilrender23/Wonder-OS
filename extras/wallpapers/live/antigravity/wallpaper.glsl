// Antigravity — ported from React Bits <Antigravity /> (see ../LICENSE-REACTBITS.md).
// The CPU spring simulation becomes closed form: the ring centre follows
// the component's auto-animate path (with a fixed lag), ambient particles
// sit on the jittered home grid (2x2 cells nearest the pixel, radially
// inverse-warped where the magnet pulls them in), and the particles captured
// into the ring are laid out on angular slots. Cursor, click burst and intro
// are dropped.

const float WORLD_HEIGHT = 31.5;
const float TAU = 6.28318530718;

vec3 stop_color(int i) {
    if (i <= 0) return P_COLOR_1;
    if (i == 1) return P_COLOR_2;
    if (i == 2) return P_COLOR_3;
    return P_COLOR_4;
}

vec3 srgb_to_lin(vec3 c) { return wl_to_linear(c); }

vec3 to_oklab(vec3 c) {
    vec3 r = srgb_to_lin(c);
    float l = pow(max(0.4122214708 * r.r + 0.5363325363 * r.g + 0.0514459929 * r.b, 0.0), 1.0 / 3.0);
    float m = pow(max(0.2119034982 * r.r + 0.6806995451 * r.g + 0.1073969566 * r.b, 0.0), 1.0 / 3.0);
    float s = pow(max(0.0883024619 * r.r + 0.2817188376 * r.g + 0.6299787005 * r.b, 0.0), 1.0 / 3.0);
    return vec3(0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
                1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s);
}

vec3 from_oklab(vec3 lab) {
    float l = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s = lab.x - 0.0894841775 * lab.y - 1.291485548 * lab.z;
    l = l * l * l; m = m * m * m; s = s * s * s;
    vec3 lin = vec3(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                    -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s);
    return wl_to_srgb(lin);
}

// Palette stop in OKLab, deepened for light mode like buildPalette().
vec3 stop_lab(int i) {
    vec3 lab = to_oklab(stop_color(i));
#if P_LIGHT_MODE
    float hue = atan(lab.z, lab.y) * 57.29578;
    float warmth = clamp(1.0 - abs(hue - 85.0) / 40.0, 0.0, 1.0);
    float ceiling = 0.62 + 0.14 * warmth * warmth * (3.0 - 2.0 * warmth);
    if (lab.x > ceiling) {
        float boost = 1.0 + (lab.x - ceiling) * 1.6;
        lab = vec3(ceiling, lab.y * boost, lab.z * boost);
    }
#endif
    return lab;
}

vec3 lab_mix(vec3 a, vec3 b, float k) {
    float e = k * k * (3.0 - 2.0 * k);
    float chroma = length(a.yz) * (1.0 - e) + length(b.yz) * e;
    vec2 ab = mix(a.yz, b.yz, e);
    float len = length(ab);
    ab *= len > 1e-4 ? chroma / len : 0.0;
    return from_oklab(vec3(mix(a.x, b.x, e), ab));
}

// Row 0 (conic, cyclic) and row 1 (radial) of the palette texture.
vec3 palette(float u, bool cyclic) {
    int n = P_COLORS;
    float segs = cyclic ? float(n) : float(n - 1);
    if (n < 2) return from_oklab(stop_lab(0));
    float t = (cyclic ? fract(u) : clamp(u, 0.0, 1.0)) * segs;
    float from = min(floor(t), segs - 1.0);
    float k = t - from;
    int fi = int(from);
    int ti = fi + 1;
    if (cyclic && ti >= n) ti = 0;
    return lab_mix(stop_lab(fi), stop_lab(ti), k);
}

vec2 cursor_at(float t, vec2 size) {
#if P_AUTO_ANIMATE
    return vec2(size.x * 0.5 + sin(t * 0.5) * size.x * 0.25,
                size.y * 0.5 + cos(t) * size.y * 0.25);
#else
    return size * 0.5;
#endif
}

float shape_sdf(vec2 p, vec2 b) {
#if P_PARTICLE_SHAPE == 0
    float r = b.y;
    return length(vec2(max(abs(p.x) - max(b.x - r, 0.0), 0.0), p.y)) - r;
#elif P_PARTICLE_SHAPE == 1
    return length(p) - b.x;
#elif P_PARTICLE_SHAPE == 2
    float r = b.x * 0.28;
    vec2 q = abs(p) - (b - r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
#else
    float r = b.x * 1.15;
    vec2 q = vec2(r * 0.5774 - p.x, abs(p.y));
    return max(dot(q, vec2(0.5, 0.8660254)) - r * 0.5, -q.x - r * 0.5) * 1.2;
#endif
}

// One particle, as the component's vertex + fragment shader pair.
void splat(vec2 frag, vec2 pos, vec2 vel, vec2 cursor, vec4 seed, float unit,
           float time, float streak_px, vec3 rgb, inout vec4 acc) {
    float ringR = P_RING_RADIUS * unit;
    float bandW = max(unit * 6.0, 1.0);
    vec2 rel = pos - cursor;
    float dist = length(rel);
    vec2 radial = dist > 1e-3 ? rel / dist : vec2(1.0, 0.0);
    vec2 dir = radial;
    float speed = length(vel);
    float streak = clamp(speed / streak_px, 0.0, 2.5);
    if (speed > 1e-3) {
        vec2 heading = vel / speed * sign(dot(vel, radial) + 1e-4);
        dir = normalize(mix(radial, heading, smoothstep(0.0, 1.0, streak)));
    }
    float rx = (dist - ringR) / bandW;
    float ring = exp(-rx * rx);
    float z = seed.x;
    float depth = 1.0 + z * 0.32 * P_DEPTH_FACTOR;
    float pulse = 1.0 + sin(time * P_PULSE_SPEED + seed.y) * 0.22 * P_PARTICLE_VARIANCE;
    float grow = P_AMBIENT + (1.0 - P_AMBIENT) * ring;
    float size = P_PARTICLE_SIZE * unit * 0.6 * depth * pulse * (0.85 + 0.3 * seed.w);
    vec2 bounds;
#if P_PARTICLE_SHAPE == 0
    float w = size * 0.17 * mix(0.55, 1.0, ring);
    bounds = vec2(max(w, size * 0.5 * grow) * (1.0 + streak * 1.2), w);
#elif P_PARTICLE_SHAPE == 1
    bounds = vec2(size * 0.22 * mix(0.45, 1.0, grow));
#else
    bounds = vec2(size * 0.26 * mix(0.45, 1.0, grow));
#endif
    float soft = 1.0 + max(-z, 0.0) * 1.6 * P_DEPTH_FACTOR;
    vec2 d = frag - pos;
    vec2 local = vec2(dot(d, dir), dot(d, vec2(-dir.y, dir.x)));
    if (abs(local.x) > bounds.x + soft * 2.0 || abs(local.y) > max(bounds.x, bounds.y) + soft * 2.0) return;
    float sd = shape_sdf(local, bounds);
    float fog = 1.0 - max(-z, 0.0) * 0.35 * P_DEPTH_FACTOR;
    float alpha = clamp(0.5 - sd / soft, 0.0, 1.0) * mix(P_AMBIENT, 1.0, ring) * fog;
    if (alpha < 0.002) return;
    acc.rgb += rgb * alpha * (1.0 - acc.a);
    acc.a += alpha * (1.0 - acc.a);
}

vec3 particle_color(float u_conic, float u_radial, float seedz) {
#if P_COLOR_MODE == 0
    return palette(u_conic, true);
#elif P_COLOR_MODE == 1
    return palette(u_radial, false);
#else
    float stops = float(P_COLORS);
    float k = floor(seedz * stops);
    return from_oklab(stop_lab(int(k)));
#endif
}

void main() {
    vec2 res = wl_res();
    vec2 f = wl_frag();
    vec2 frag = vec2(f.x, res.y - f.y);  // top-left origin, like the canvas
    vec2 size = res;
    float time = wl_time();
    float unit = size.y / WORLD_HEIGHT;

    // The particles trail the drifting cursor by roughly two spring lags.
    float lagged = time - 0.45;
    vec2 cursor = cursor_at(lagged, size);
    vec2 cvel = (cursor_at(lagged + 0.05, size) - cursor_at(lagged - 0.05, size)) / 0.1;
    float streak_px = unit * 30.0;

    float ringR = P_RING_RADIUS * unit;
    float magnet = max(P_MAGNET_RADIUS * unit, 1.0);
    float edge = magnet * 0.18;
    float spread = (5.0 / (P_FIELD_STRENGTH + 0.1)) * unit;
    float wavePx = P_WAVE_AMPLITUDE * unit * 0.5;
    float hue = time * P_COLOR_SPEED;
    float waveCount = float(P_WAVE_COUNT);

    vec2 rel = frag - cursor;
    float r = length(rel);
    float ang = atan(rel.y, rel.x);
    vec3 conic_base = vec3(0.0);
    float u_conic = ang / TAU + hue;
    float u_radial = clamp(r / (ringR * 2.4), 0.0, 1.0);
    vec3 pix_color = particle_color(u_conic, u_radial, 0.0);

    vec4 acc = vec4(0.0);

    // ---- particles captured into the ring: angular slots ----
    float margin = 0.06;
    vec2 span = size * (1.0 + margin * 2.0);
    float density = float(P_COUNT) / (span.x * span.y);
    float inner = max(magnet - edge, 0.0);
    float captured = density * 3.14159265 * inner * inner;
    float spin = P_ROTATION_SPEED * time;
    if (captured >= 1.0) {
        float slots = max(floor(captured * 0.5), 1.0);
        float a0 = (ang - spin) / TAU * slots;
        float k0 = floor(a0);
        for (int dk = -1; dk <= 1; dk++) {
            float k = mod(k0 + float(dk), slots);
            for (int j = 0; j < 2; j++) {
                vec2 key = vec2(k, float(j) * 131.0 + 17.0);
                vec2 h1 = wl_hash22(key + 3.1);
                vec2 h2 = wl_hash22(key + 47.7);
                vec2 h3 = wl_hash22(key + 91.3);
                float theta = (k + 0.5 + (h1.x - 0.5) * 0.9) / slots * TAU + spin;
                float band = h1.y * 2.0 - 1.0;
                float phase = h2.x * TAU;
                vec4 seed = vec4(h2.y * 2.0 - 1.0, phase, h3.x, h3.y);
                float parallax = 1.0 - seed.x * 0.12 * P_DEPTH_FACTOR;
                vec2 c = size * 0.5 + (cursor - size * 0.5) * parallax;
                float wave = wavePx * (0.6 * sin(theta * waveCount - time * P_WAVE_SPEED * 2.0)
                                     + 0.4 * sin(phase + time * P_WAVE_SPEED * 1.3));
                float radius = ringR + wave + band * spread;
                vec2 pos = c + vec2(cos(theta), sin(theta)) * radius;
                float u_c = theta / TAU + hue + (seed.z - 0.5) * 0.06;
#if P_COLOR_MODE == 0
                vec3 rgb = pix_color;
#else
                vec3 rgb = particle_color(u_c, radius / (ringR * 2.4) + (seed.z - 0.5) * 0.06, seed.z);
#endif
                splat(frag, pos, cvel, cursor, seed, unit, time, streak_px, rgb, acc);
            }
        }
    }

    // ---- ambient particles on the home grid ----
    // Undo the magnet's radial pull to find where the pixel's particles live.
    float wave0 = wavePx * 0.6 * sin((ang - spin) * waveCount - time * P_WAVE_SPEED * 2.0);
    float ringW = ringR + wave0;
    float home_r = r;
    bool hollow = false;
    if (r < magnet + edge) {
        if (r < ringW) {
            hollow = true;
        } else {
            float lo = inner;
            float hi = magnet + edge;
            for (int i = 0; i < 8; i++) {
                float mid = 0.5 * (lo + hi);
                float cap = 1.0 - clamp((mid - (magnet - edge)) / (edge * 2.0), 0.0, 1.0);
                float e = cap * cap * (3.0 - 2.0 * cap);
                float rad = mid + (ringW - mid) * e;
                if (rad < r) lo = mid; else hi = mid;
            }
            home_r = 0.5 * (lo + hi);
        }
    }
    if (!hollow) {
        vec2 home_p = cursor + (r > 1e-3 ? rel / r : vec2(1.0, 0.0)) * home_r;
        float cols = max(1.0, ceil(sqrt(float(P_COUNT) * span.x / span.y)));
        float rows = max(1.0, ceil(float(P_COUNT) / cols));
        vec2 grid = vec2(cols, rows);
        vec2 cellSize = span / grid;
        vec2 gpos = (home_p + size * margin) / cellSize;
        // Particles reach < 0.7 cell from their cell centre, so the 2x2
        // block of cells nearest the pixel covers every one that can touch it.
        vec2 base = floor(gpos - 0.5);
        float fill = float(P_COUNT) / (cols * rows);
        for (int gy = 0; gy <= 1; gy++) {
            for (int gx = 0; gx <= 1; gx++) {
                vec2 cell = base + vec2(float(gx), float(gy));
                vec2 h0 = wl_hash22(cell + 0.37);
                if (h0.x > fill) continue;
                vec2 h1 = wl_hash22(cell * 1.31 + 7.7);
                vec2 h2 = wl_hash22(cell * 0.77 + 19.3);
                vec2 h3 = wl_hash22(cell * 1.93 + 41.9);
                vec4 seed = vec4(h2.x * 2.0 - 1.0, h1.y * TAU, h3.x, h3.y);
                float band = h2.y * 2.0 - 1.0;
                float phase = seed.y;
                vec2 home = (cell + 0.5 + (h1 - 0.5) * 0.85) * cellSize - size * margin;
                home.y += sin(time * 0.7 + phase) * unit * 0.12 * P_WAVE_AMPLITUDE;
                float parallax = 1.0 - seed.x * 0.12 * P_DEPTH_FACTOR;
                vec2 c = size * 0.5 + (cursor - size * 0.5) * parallax;
                vec2 dh = home - c;
                float dist = length(dh);
                if (dist < inner) continue;  // drawn on the ring slots
                vec2 pos = home;
                vec2 vel = vec2(0.0);
                float cap = 1.0 - clamp((dist - (magnet - edge)) / (edge * 2.0), 0.0, 1.0);
                float e = cap * cap * (3.0 - 2.0 * cap);
                float u_c = atan(dh.y, dh.x) / TAU + hue + (seed.z - 0.5) * 0.06;
                if (e > 0.0) {
                    float angle = atan(dh.y, dh.x);
                    float wave = wavePx * (0.6 * sin(angle * waveCount - time * P_WAVE_SPEED * 2.0)
                                         + 0.4 * sin(phase + time * P_WAVE_SPEED * 1.3));
                    float radius = dist + (ringR + wave + band * spread - dist) * e;
                    pos = c + dh / dist * radius;
                    vel = cvel * e;
                }
#if P_COLOR_MODE == 0
                vec3 rgb = pix_color;
#else
                vec3 rgb = particle_color(u_c, length(pos - cursor) / (ringR * 2.4) + (seed.z - 0.5) * 0.06, seed.z);
#endif
                splat(frag, pos, vel, cursor, seed, unit, time, streak_px, rgb, acc);
            }
        }
    }

    gl_FragColor = vec4(wl_over(P_BACKGROUND, acc), 1.0);
}
