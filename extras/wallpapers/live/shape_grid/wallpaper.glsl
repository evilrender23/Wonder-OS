// ShapeGrid — ported from React Bits <ShapeGrid /> (see ../LICENSE-REACTBITS.md).
// The canvas-2D grid becomes per-pixel SDFs over the neighbouring cells.
// No pointer: an optional wandering light (the component's hover spot) lights
// the cells, and its decaying trail is rebuilt from a few past positions.
// shadowBlur glow → gaussian falloff of the stroke; intro/clicks dropped.

const float SQRT3 = 1.7320508;
const int TRAIL_PTS = 8;

float smootherstep01(float t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float hash_cell(float i, float j) {
    return fract(sin(i * 127.1 + j * 311.7) * 43758.5453);
}

vec3 hsl(float hue, float s, float l) {
    vec3 k = clamp(abs(mod(hue / 60.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return l + s * (k - 0.5) * (1.0 - abs(2.0 * l - 1.0));
}

float sd_box(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sd_round_box(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Flat-top hexagon with circumradius R (vertex on the x axis).
float sd_hexagon(vec2 p, float R) {
    float r = R * 0.8660254;
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}

// Equilateral triangle, half side r, centroid at origin, apex up (y up).
float sd_triangle(vec2 p, float r) {
    const float k = 1.7320508;
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}

float sd_segment_x(vec2 p, float arm) {
    return length(vec2(p.x - clamp(p.x, -arm, arm), p.y));
}

vec2 step_xy() {
    float size = P_CELL_SIZE;
#if P_SHAPE == 2
    return vec2(size / SQRT3 * 1.5, size);
#elif P_SHAPE == 3
    return vec2(size / SQRT3, size);
#else
    return vec2(size);
#endif
}

vec2 center_of(float i, float j) {
    vec2 st = step_xy();
#if P_SHAPE == 2
    return vec2(i * st.x, j * st.y + (mod(abs(i), 2.0) > 0.5 ? st.y * 0.5 : 0.0));
#elif P_SHAPE == 3
    bool up = mod(i + j, 2.0) < 0.5;
    return vec2(i * st.x, j * st.y + (up ? st.y * (2.0 / 3.0) : st.y / 3.0));
#elif P_SHAPE == 6 || P_SHAPE == 7
    return vec2(i * st.x, j * st.y);
#else
    return vec2((i + 0.5) * st.x, (j + 0.5) * st.y);
#endif
}

// Signed distance of the cell outline/area (y-down local coords).
float cell_sd(vec2 q, float inset, float i, float j) {
    float size = P_CELL_SIZE;
#if P_SHAPE == 1
    float half_s = size * 0.5 - max(inset, size * 0.06);
    float r = min(half_s, size * 0.22);
    return sd_round_box(q, vec2(half_s), r);
#elif P_SHAPE == 2
    return sd_hexagon(q, size / SQRT3 - inset);
#elif P_SHAPE == 3
    bool up = mod(i + j, 2.0) < 0.5;
    float side = size * 2.0 / SQRT3;
    float r = side * 0.5 - inset * 1.7;
    return sd_triangle(vec2(q.x, up ? -q.y : q.y), r);
#elif P_SHAPE == 4
    float r = size * 0.5 - inset;
    return (abs(q.x) + abs(q.y) - r) * 0.70710678;
#elif P_SHAPE == 5
    return length(q) - max(0.5, size * 0.5 - inset);
#elif P_SHAPE == 6
    float arm = size * 0.16 + inset;
    return min(sd_segment_x(q, arm), sd_segment_x(q.yx, arm));
#elif P_SHAPE == 7
    return length(q) - (size * 0.05 + inset);
#else
    float half_s = size * 0.5 - inset;
    return sd_box(q, vec2(half_s));
#endif
}

// Tilt homography (the component's viewFor, around the centre).
vec3 tilt_inverse(vec2 s, vec2 size) {
    float ax = radians(P_TILT_X);
    float ay = radians(P_TILT_Y);
    float depth = max(size.x, size.y) * 1.25;
    vec2 q = s - size * 0.5;
    float u0 = q.x / cos(ay);
    float u1 = (q.y - sin(ax) * sin(ay) * u0) / cos(ax);
    float u2 = 1.0 - (sin(ay) * cos(ax) / depth) * u0 + (sin(ax) / depth) * u1;
    return vec3(u0, u1, u2);
}

vec2 direction() {
#if P_DIRECTION == 0
    return vec2(0.70710678, -0.70710678);
#elif P_DIRECTION == 1
    return vec2(0.0, -1.0);
#elif P_DIRECTION == 2
    return vec2(0.0, 1.0);
#elif P_DIRECTION == 3
    return vec2(-1.0, 0.0);
#elif P_DIRECTION == 4
    return vec2(1.0, 0.0);
#else
    return vec2(0.0);
#endif
}

vec4 over(vec4 dst, vec3 color, float a) {
    a = clamp(a, 0.0, 1.0);
    return vec4(color * a, a) + dst * (1.0 - a);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float t = wl_time();
    vec2 S = vec2(frag.x, res.y - frag.y);
    float size = P_CELL_SIZE;
    float lw = P_LINE_WIDTH;

    // Screen → grid plane.
    vec2 plane = S;
    float aa = 1.0;
    float mist = 1.0;
    if (abs(P_TILT_X) + abs(P_TILT_Y) > 0.0) {
        float ax = radians(P_TILT_X);
        float ay = radians(P_TILT_Y);
        float facing = cos(ax) * cos(ay);
        float farW = clamp(sqrt(facing * size / 5.0), 1.15, 2.5);
        float nearW = min(sqrt(facing * size / 14.0), farW - 0.35);
        vec3 u = tilt_inverse(S, res);
        if (u.z < 1.0 / farW) {
            gl_FragColor = vec4(P_BACKGROUND, 1.0);
            return;
        }
        plane = res * 0.5 + u.xy / u.z;
        vec3 ux = tilt_inverse(S + vec2(1.0, 0.0), res);
        vec3 uy = tilt_inverse(S + vec2(0.0, 1.0), res);
        vec2 dx = res * 0.5 + ux.xy / ux.z - plane;
        vec2 dy = res * 0.5 + uy.xy / uy.z - plane;
        aa = max(length(abs(dx) + abs(dy)) * 0.7071, 0.0001);
        mist = 1.0 - smootherstep01((1.0 / u.z - nearW) / (farW - nearW));
    }

    // The component's speed (0.5) × 60 px/s, folded in.
    vec2 dir = direction();
    vec2 offset = -dir * 30.0 * t;
    vec2 world = plane + offset;

    // Wandering spot and its trail (world coordinates).
    float reach = max(P_RADIUS, 0.5) * size;
    float trailSpan = P_TRAIL * 3.0;
    float dtau = trailSpan / float(TRAIL_PTS - 1);
    float amount = min(res.x, res.y) * 0.32;
    vec2 pts[TRAIL_PTS];
    float nearTrail = 1e9;
    for (int k = 0; k < TRAIL_PTS; k++) {
        float tk = t - float(k) * dtau;
        vec2 spot = res * 0.5 + vec2(cos(tk * 0.37) * amount + sin(tk * 0.21) * amount * 0.35,
                                     sin(tk * 0.53 + 1.3) * amount * 0.8);
        pts[k] = spot - dir * 30.0 * tk;
    }
#if P_WANDER
    for (int k = 0; k < TRAIL_PTS - 1; k++) {
        vec2 a = pts[k];
        vec2 ba = pts[k + 1] - a;
        float h = clamp(dot(world - a, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
        nearTrail = min(nearTrail, length(world - a - ba * h));
    }
#endif
    bool lightable = nearTrail < reach + size * 2.0;

    vec2 st = step_xy();
    float i0;
    float j0;
#if P_SHAPE == 2
    i0 = floor(world.x / st.x + 0.5);
    j0 = floor(world.y / st.y + 0.25);
#elif P_SHAPE == 3
    i0 = floor(world.x / st.x + 0.5);
    j0 = floor(world.y / st.y);
#elif P_SHAPE == 6 || P_SHAPE == 7
    i0 = floor(world.x / st.x + 0.5);
    j0 = floor(world.y / st.y + 0.5);
#else
    i0 = floor(world.x / st.x);
    j0 = floor(world.y / st.y);
#endif

    float tone = clamp((max(P_HOVER_COLOR.r, max(P_HOVER_COLOR.g, P_HOVER_COLOR.b))
                      + min(P_HOVER_COLOR.r, min(P_HOVER_COLOR.g, P_HOVER_COLOR.b))) * 0.5, 0.45, 0.68);
    bool markers = P_SHAPE == 6 || P_SHAPE == 7;
    float baseDist = 1e9;
    vec4 lit = vec4(0.0);

    for (int di = -2; di <= 2; di++) {
#if P_SHAPE != 3
        if (di == -2 || di == 2) continue;
#endif
        for (int dj = -1; dj <= 1; dj++) {
            float i = i0 + float(di);
            float j = j0 + float(dj);
            vec2 cw = center_of(i, j);
            vec2 q = world - cw;
            baseDist = min(baseDist, P_SHAPE == 7 ? cell_sd(q, 0.0, i, j) : abs(cell_sd(q, 0.0, i, j)));

            // Light of this cell: trail, twinkle.
            float value = 0.0;
            float hue = 0.0;
#if P_WANDER
            if (lightable) {
                for (int k = 0; k < TRAIL_PTS - 1; k++) {
                    vec2 a = pts[k];
                    vec2 ba = pts[k + 1] - a;
                    float h = clamp(dot(cw - a, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
                    float d = length(cw - a - ba * h);
                    float v = P_RADIUS > 0.0 ? smoothstep(reach, reach * 0.15, d) : 0.0;
                    v = max(v, smoothstep(size * 0.6, size * 0.4, d));
                    float tau = (float(k) + h) * dtau;
                    v *= exp(-tau / max(P_TRAIL, 0.05));
                    if (v > value) {
                        value = v;
                        hue = (t - tau) * 120.0 + (cw.x + cw.y) / size * 12.0;
                    }
                }
            }
#endif
            if (P_TWINKLE > 0.0) {
                float seed = hash_cell(i, j);
                float wave = sin(t * (0.35 + seed * 0.5) + seed * 40.0);
                float sparkle = P_TWINKLE * 0.7 * pow(max(0.0, wave), 18.0);
                if (sparkle > value) {
                    value = sparkle;
                    hue = seed * 360.0;
                }
            }
            if (value <= 0.01) continue;

            vec3 color = P_HOVER_COLOR;
#if P_RAINBOW
            color = hsl(hue, 0.9, tone);
#endif
            float v1 = min(1.0, value);
            if (markers) {
                float sd = cell_sd(q, value * size * 0.08, i, j);
                float sigma = max(P_GLOW * 14.0 * value * 0.5, 0.001);
#if P_SHAPE == 7
                float cov = clamp(0.5 - sd / aa, 0.0, 1.0);
                float shadow = P_GLOW > 0.0 ? exp(-max(sd, 0.0) * max(sd, 0.0) / (2.0 * sigma * sigma)) * 0.6 : 0.0;
#else
                float w = lw * (1.0 + value * 0.6);
                float cov = clamp((w * 0.5 - sd) / aa + 0.5, 0.0, 1.0);
                float shadow = P_GLOW > 0.0 ? min(1.0, w / (sigma * 2.5066)) * exp(-sd * sd / (2.0 * sigma * sigma)) : 0.0;
#endif
                lit = over(lit, color, v1 * shadow);
                lit = over(lit, color, v1 * cov);
            } else {
                float sd = cell_sd(q, lw * 0.5, i, j);
                if (P_FILL_OPACITY > 0.0) {
                    float inside = clamp(0.5 - sd / aa, 0.0, 1.0);
#if P_RAINBOW
                    lit = over(lit, color, v1 * P_FILL_OPACITY * inside);
#else
                    lit = over(lit, P_HOVER_FILL_COLOR, v1 * P_FILL_OPACITY * inside);
#endif
                }
                float ad = abs(sd);
                float cov = clamp((lw * 0.5 - ad) / aa + 0.5, 0.0, 1.0);
                if (P_GLOW > 0.0) {
                    float sigma = max(P_GLOW * 16.0 * value * 0.5, 0.001);
                    float shadow = min(1.0, lw / (sigma * 2.5066)) * exp(-ad * ad / (2.0 * sigma * sigma));
                    lit = over(lit, color, v1 * 0.85 * shadow);
                }
                lit = over(lit, color, v1 * 0.85 * cov);
            }
        }
    }

    // Base grid in the line colour.
    float baseCov;
#if P_SHAPE == 0
    vec2 g = abs(world - floor(world / size + 0.5) * size);
    baseCov = clamp((lw * 0.5 - min(g.x, g.y)) / aa + 0.5, 0.0, 1.0);
#elif P_SHAPE == 7
    baseCov = clamp(0.5 - baseDist / aa, 0.0, 1.0);
#else
    baseCov = clamp((lw * 0.5 - baseDist) / aa + 0.5, 0.0, 1.0);
#endif
    vec4 layer = vec4(P_LINE_COLOR * baseCov, baseCov);
    layer = lit + layer * (1.0 - lit.a);

    // CSS edge mask (fade × 26 % per side) and depth mist.
    vec2 uv = S / res;
    float edge = P_FADE * 0.26;
    float mask = 1.0;
    if (edge > 0.0) {
        mask = smootherstep01(uv.x / edge) * smootherstep01((1.0 - uv.x) / edge)
             * smootherstep01(uv.y / edge) * smootherstep01((1.0 - uv.y) / edge);
    }
    layer *= mask * mist;
    gl_FragColor = vec4(wl_over(P_BACKGROUND, layer), 1.0);
}
