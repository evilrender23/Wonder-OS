// AeroShards — ported from React Bits <AeroShards /> (see ../LICENSE-REACTBITS.md).
// The original is a WebGPU scene: ~1300 instanced glass prisms ray-traced per
// pixel, five depth layers, per-layer depth-of-field blur, survey markers,
// bloom chain and a finish pass. Here, in one fragment pass: each of the five
// ribbons is a screen-space band around its analytic path; the shards are
// placed one per cell of a (along, across) grid that flows along the band and
// only the few neighbouring cells are evaluated. Each shard is a bevelled
// glass plate facing the camera (bank / pitch / yaw like the original) shaded
// with the same studio environment, Fresnel and one refracted exit ray;
// depth of field widens the edges, bloom is a soft halo around the rims.
// Pointer parting, camera lean, click rings, hold-to-gather and the intro
// are dropped.

const float TAU = 6.28318530718;
const float PI = 3.14159265359;
const float CAMERA = 4.0;

float pick5(float l, float a, float b, float c, float d, float e) {
    if (l < 0.5) return a;
    if (l < 1.5) return b;
    if (l < 2.5) return c;
    if (l < 3.5) return d;
    return e;
}

float depth_of(float l) { return pick5(l, 2.7, 3.9, 5.6, 8.1, 11.6); }
float share_of(float l) { return pick5(l, 0.26, 0.23, 0.2, 0.17, 0.14); }
float growth_of(float l) { return pick5(l, 1.0, 1.3, 1.8, 2.6, 3.6); }
float fade_of(float l) { return pick5(l, 1.0, 0.8, 0.52, 0.32, 0.2); }

float smoother(float x) {
    float t = clamp(x, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float hsh(vec2 id, float k) {
    return wl_hash12(id * vec2(1.0, 1.37) + vec2(k * 17.31 + 3.7, k * 7.13 + 11.9));
}

// ---------------------------------------------------------------- ribbons

struct Rib {
    float s;        // path parameter 0..1
    float a;        // along coordinate (screen units of the param axis)
    float c;        // signed distance across the band (screen units)
    float stretch;  // screen length per unit of a
    vec2 T;         // screen tangent (direction of travel)
    float d;        // depth
    float len;      // screen length of the param axis
    float hw;       // half width of the band (screen units)
    float taper;
    float angle;    // twist angle
};

// Path of ribbon l at parameter s: screen point (xy) and d/ds (zw).
// Screen units: y spans [-1, 1], x spans [-aspect, aspect] (focal = 2H).
vec4 path_at(float l, float s, float aspect) {
    float sc = P_SCALE;
    if (P_PLACEMENT == 0) {
        float s0 = pick5(l, 0.2, -0.72, 0.58, -0.24, 0.8);
        float s1 = pick5(l, -0.78, 0.02, -0.06, 0.62, 0.3);
        float v = s0 + (s1 - s0) * s + 0.1 * sin(s * TAU * 0.7 + l * 2.3) + 0.045 * sin(s * TAU * 1.9 + l);
        float dv = (s1 - s0) + 0.1 * TAU * 0.7 * cos(s * TAU * 0.7 + l * 2.3) + 0.045 * TAU * 1.9 * cos(s * TAU * 1.9 + l);
        return vec4((-1.32 + 2.64 * s) * aspect * sc, v * sc, 2.64 * aspect * sc, dv * sc);
    }
    if (P_PLACEMENT == 3) {
        float A = pick5(l, 0.98, 0.9, 0.84, 0.8, 0.76) * aspect * sc;
        float B = pick5(l, 0.78, 0.7, 0.64, 0.6, 0.56) * sc;
        float th = s * TAU + l * 0.9 + PI * 0.5;
        return vec4(cos(th) * A, sin(th) * B - 0.02 * sc, -sin(th) * A * TAU, cos(th) * B * TAU);
    }
    float mirror = P_PLACEMENT == 1 ? -1.0 : 1.0;
    float off = pick5(l, 0.84, 0.62, 0.44, 0.28, 0.14);
    float u = mirror * (off + 0.1 * sin(s * TAU * 0.6 + l * 1.3) + 0.03 * sin(s * TAU * 2.1 + l) - 0.08 * (1.0 - s));
    float du = mirror * (0.1 * TAU * 0.6 * cos(s * TAU * 0.6 + l * 1.3) + 0.03 * TAU * 2.1 * cos(s * TAU * 2.1 + l) + 0.08);
    return vec4(u * aspect * sc, (1.32 - 2.64 * s) * sc, du * aspect * sc, -2.64 * sc);
}

float depth_at(float l, float s) {
    float D = depth_of(l);
    if (P_PLACEMENT == 0) return D + 0.35 * sin(s * PI * 1.4 + l * 1.9);
    if (P_PLACEMENT == 3) return D * 1.1 - sin(s * TAU + l * 0.9 + PI * 0.5) * (0.5 + l * 0.25);
    return D + 0.3 * sin(s * PI * 1.2 + l);
}

Rib ribbon(float l, vec2 q, float aspect, float t) {
    Rib r;
    float sc = P_SCALE;
    float s;
    if (P_PLACEMENT == 0) {
        s = (q.x / (aspect * sc) + 1.32) / 2.64;
        r.len = 2.64 * aspect * sc;
    } else if (P_PLACEMENT == 3) {
        float A = pick5(l, 0.98, 0.9, 0.84, 0.8, 0.76) * aspect * sc;
        float B = pick5(l, 0.78, 0.7, 0.64, 0.6, 0.56) * sc;
        float th = atan((q.y + 0.02 * sc) / B, q.x / A);
        s = (th - l * 0.9 - PI * 0.5) / TAU;
        r.len = PI * (3.0 * (A + B) - sqrt((3.0 * A + B) * (A + 3.0 * B)));
    } else {
        s = (1.32 - q.y / sc) / 2.64;
        r.len = 2.64 * sc;
    }
    // Closest point on the path (two Gauss-Newton steps), so the band
    // coordinates are orthogonal and shards keep their shape.
    vec4 pp = path_at(l, s, aspect);
    s += dot(q - pp.xy, pp.zw) / dot(pp.zw, pp.zw);
    pp = path_at(l, s, aspect);
    s += dot(q - pp.xy, pp.zw) / dot(pp.zw, pp.zw);
    pp = path_at(l, s, aspect);
    if (P_PLACEMENT == 3) s = fract(s);
    r.s = s;
    r.a = s * r.len;
    float speed = length(pp.zw);
    r.T = pp.zw / speed;
    r.stretch = speed / r.len;
    r.d = depth_at(l, s);
    r.taper = P_PLACEMENT == 3 ? 1.0 : smoother(s / 0.12) * smoother((1.0 - s) / 0.12);
    vec2 N = vec2(-r.T.y, r.T.x);
    // Sway of the band (billow along the across direction).
    float sway = 0.06 * P_BILLOW * sc * sin(r.s * TAU * 0.8 - t * 0.35 + l * 2.4);
    r.c = dot(q - pp.xy, N) - sway;
    r.angle = P_TWIST * (r.s * TAU * 0.6 + t * 0.08 + l * 1.7);
    float width = P_SPREAD * (0.15 + 0.035 * depth_of(l)) * (0.8 + 0.3 * sin(r.s * TAU * 1.3 + l * 0.8));
    r.hw = width * CAMERA * sc / r.d * max(abs(cos(r.angle)), 0.25);
    return r;
}

// Half width of the band and twist angle at path parameter s (per shard).
vec2 band_at(float l, float s, float t) {
    float d = depth_at(l, s);
    float angle = P_TWIST * (s * TAU * 0.6 + t * 0.08 + l * 1.7);
    float width = P_SPREAD * (0.15 + 0.035 * depth_of(l)) * (0.8 + 0.3 * sin(s * TAU * 1.3 + l * 0.8));
    return vec2(width * CAMERA * P_SCALE / d * max(abs(cos(angle)), 0.25), angle);
}

// ---------------------------------------------------------------- lighting

float lobe(vec3 r, vec3 dir, float width) {
    return exp((dot(r, dir) - 1.0) / (width * width));
}

float rect_light(vec3 r, vec3 dir, vec3 across, vec2 extent, float soft) {
    float facing = dot(r, dir);
    if (facing <= 0.0) return 0.0;
    vec3 up = cross(dir, across);
    float u = dot(r, across) / facing;
    float v = dot(r, up) / facing;
    float a = 1.0 - smoothstep(extent.x - soft, extent.x + soft, abs(u));
    float b = 1.0 - smoothstep(extent.y - soft, extent.y + soft, abs(v));
    return a * b;
}

vec3 KEY_DIR() { return vec3(-0.2791, 0.7177, 0.6380); }

vec3 studio(vec3 r, float soft, vec3 accent, vec3 ink) {
    float sky = clamp(r.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 col = mix(ink * 0.03, ink * 0.12 + accent * 0.03, smoothstep(0.25, 0.95, sky));
    col += mix(ink, vec3(1.0), 0.6) * lobe(r, KEY_DIR(), 0.38) * 3.6;
    col += mix(accent, vec3(1.0), 0.2) * lobe(r, vec3(0.7827, 0.2208, -0.5820), 0.34) * 3.4;
    col += vec3(1.0, 0.98, 1.0) * rect_light(r, vec3(-0.8583, 0.1198, 0.4990), vec3(0.5026, 0.0, 0.8645), vec2(0.12, 0.8), 0.08 + soft) * 3.2;
    col += vec3(1.0, 0.98, 1.0) * rect_light(r, vec3(0.0501, 0.9424, -0.3308), vec3(1.0, 0.0, 0.0), vec2(0.9, 0.1), 0.08 + soft) * 2.6;
    return col;
}

vec3 studio_light(vec3 r, float soft) {
    vec3 col = mix(vec3(0.2, 0.2, 0.23), vec3(0.97, 0.97, 0.99), smoothstep(-0.08 - soft, 0.16 + soft, r.y));
    col = mix(col, vec3(0.55, 0.55, 0.6), lobe(r, vec3(0.0, -1.0, 0.0), 0.5) * 0.4);
    col += vec3(0.4) * rect_light(r, KEY_DIR(), vec3(0.9171, 0.0, 0.3987), vec2(0.46, 0.3), 0.07 + soft);
    col -= vec3(0.55) * rect_light(r, vec3(-0.8583, 0.1198, 0.4990), vec3(0.5026, 0.0, 0.8645), vec2(0.1, 0.85), 0.05 + soft);
    col -= vec3(0.45) * rect_light(r, vec3(0.7827, 0.2208, -0.5820), vec3(0.6, 0.0, 0.8), vec2(0.12, 0.9), 0.05 + soft);
    return clamp(col, vec3(0.05), vec3(1.2));
}

float schlick(float cosine, float f0) {
    return f0 + (1.0 - f0) * pow(1.0 - clamp(cosine, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------- markers

float bar(float value, float edge, float soft) {
    return clamp((edge - value) / soft + 0.5, 0.0, 1.0);
}

vec4 marker_layer(float l, vec2 q, vec2 res, float aspect, float t, vec3 shard, vec3 accent) {
    float D = depth_of(l);
    float sc = P_SCALE;
    float k = CAMERA * sc / D;              // world -> screen units
    float spacing = 0.42 * pow(D / CAMERA, 0.55);
    float p0 = pick5(l, 0.37, 0.83, 0.21, 0.66, 0.05);
    float p1 = pick5(l, 0.61, 0.12, 0.88, 0.44, 0.27);
    float drift = t * 0.045;
    vec2 w = q / k;
    vec2 ij = floor(vec2((w.x + drift) / spacing - p0, w.y / spacing - p1) + 0.5);
    vec2 id = ij + vec2(l * 97.0, 0.0);
    if (hsh(id, 11.0) > 0.5) return vec4(0.0);
    vec2 mw = vec2((ij.x + p0) * spacing - drift, (ij.y + p1) * spacing);
    vec2 mq = mw * k;
    float halfH = res.y * 0.5;
    vec2 local = (q - mq) * halfH;
    float blur = min(P_DEPTH_OF_FIELD * res.y * 0.03 * abs(1.0 - 3.1 / D), res.y * 0.05) * 0.18;
    float sizeMax = 4.0 * clamp(sqrt(3.1 / D), 0.6, 1.3) * 1.45;
    if (max(abs(local.x), abs(local.y)) > sizeMax * 2.6 + blur + 3.0) return vec4(0.0);

    // Closeness to this level's ribbon brightens the marker.
    Rib rb = ribbon(l, mq, aspect, t);
    float band = max(P_SPREAD * (0.15 + 0.035 * D) * 1.4, 0.02);
    float gapW = rb.c / k;
    float near = exp(-gapW * gapW / (band * band));
    float bs = hsh(id, 23.0);
    float br = hsh(id, 37.0);
    float blink = bs < 0.07 ? 0.5 + 0.5 * sin(t * (1.4 + br * 2.2) + br * 40.0) : 0.0;
    float bracket = smoother((blink - 0.55) / 0.4);

    float size = 4.0 * clamp(sqrt(3.1 / D), 0.6, 1.3) * (1.0 + 0.15 * near + 0.3 * blink);
    vec2 a = abs(local);
    float soft = 1.0 + blur;
    float thick = 0.6 + blur * 0.5;
    float energy = 0.6 / thick;
    float cover;
    if (P_MARKERS == 0) {
        float across = bar(a.x, size, soft) * bar(a.y, thick, soft);
        float down = bar(a.y, size, soft) * bar(a.x, thick, soft);
        cover = max(across, down) * energy;
    } else {
        cover = bar(length(local), size * 0.42 + blur * 0.3, soft) * mix(1.0, energy, 0.6);
    }
    if (bracket > 0.001) {
        float edge = size * 2.3;
        float span = edge * 0.45;
        float line = 0.5;
        float sides = bar(abs(a.x - edge), line, 1.0) * bar(edge - span - a.y, 0.0, 1.0) * bar(a.y - edge - line, 0.0, 1.0);
        float caps = bar(abs(a.y - edge), line, 1.0) * bar(edge - span - a.x, 0.0, 1.0) * bar(a.x - edge - line, 0.0, 1.0);
        cover = max(cover, max(sides, caps) * bracket * 0.85);
    }
    float glow = 0.42 + 0.5 * near + blink * 0.9;
    float fade = fade_of(l) * (1.0 - smoother((D - 3.0) / 10.0) * 0.45);
    vec3 tint = mix(shard, accent, 0.4);
    if (P_LIGHT_MODE == 1) {
        float ink = (1.0 - exp(-glow * 1.6)) * 0.85 * cover * fade;
        return vec4(mix(tint, accent, 0.5) * 0.55 * ink, ink);
    }
    float light = cover * glow * fade;
    vec3 color = (1.0 - exp(-mix(tint, vec3(1.0), 0.35) * light * 1.6)) * 1.4;
    return vec4(color, max(max(color.r, color.g), color.b) * 0.8);
}

// ---------------------------------------------------------------- shards

// Shade one shard at the pixel; returns premultiplied colour (alpha in .a)
// and adds its bloom halo to `halo`.
vec4 shard_at(float l, Rib rb, vec2 own, vec2 cellId, vec2 P, vec2 qc, float sizeS, float lane, vec3 rd,
              vec2 res, float sigmaPx, float t, vec3 shard, vec3 accent, inout vec3 halo) {
    float halfH = res.y * 0.5;
    // Shape: sliver triangle, kite, or a 4/5-sided chip (as createShards).
    vec2 V[6];
    float n;
    float fam = hsh(cellId, 4.0);
    if (fam < 0.55) {
        float spread = 0.17 + hsh(cellId, 5.0) * 0.15;
        V[0] = vec2(1.0, (hsh(cellId, 6.0) - 0.5) * 0.08);
        V[1] = vec2(-0.5 - hsh(cellId, 7.0) * 0.25, spread * (0.8 + hsh(cellId, 8.0) * 0.4));
        V[2] = vec2(-0.5 - hsh(cellId, 9.0) * 0.25, -spread * (0.8 + hsh(cellId, 10.0) * 0.4));
        V[3] = V[0];
        V[4] = V[0];
        V[5] = V[0];
        n = 3.0;
    } else if (fam < 0.8) {
        float spread = 0.2 + hsh(cellId, 5.0) * 0.14;
        V[0] = vec2(1.0, 0.0);
        V[1] = vec2((hsh(cellId, 6.0) - 0.3) * 0.5, spread);
        V[2] = vec2(-0.55 - hsh(cellId, 7.0) * 0.2, (hsh(cellId, 8.0) - 0.5) * 0.1);
        V[3] = vec2((hsh(cellId, 9.0) - 0.3) * 0.5, -spread * (0.8 + hsh(cellId, 10.0) * 0.4));
        V[4] = V[0];
        V[5] = V[0];
        n = 4.0;
    } else {
        n = hsh(cellId, 5.0) < 0.5 ? 4.0 : 5.0;
        float stretch = 0.4 + hsh(cellId, 6.0) * 0.3;
        float turn = hsh(cellId, 7.0) * TAU;
        for (int i = 0; i < 5; i++) {
            float ang = turn + (float(i) + (hsh(cellId, 8.0 + float(i)) - 0.5) * 0.6) * TAU / n;
            V[i] = vec2(cos(ang), sin(ang) * stretch);
        }
        V[5] = V[0];
        if (n < 4.5) V[4] = V[0];
    }
    vec2 mean = (V[0] + V[1] + V[2] + V[3] + V[4]) - V[0] * (5.0 - n);
    mean /= n;
    for (int i = 0; i < 6; i++) V[i] -= mean;

    // Orientation: axis along the flow, banked / pitched / yawed.
    float s = own.x;
    float yaw = (hsh(cellId, 13.0) - 0.5) * 0.36 + 0.05 * sin(t * (0.5 + hsh(cellId, 14.0) * 0.5) * 0.7 + hsh(cellId, 15.0) * TAU);
    float wave = 0.8 * min(1.0, P_TWIST) * sin(own.y) + 0.3 * sin(s * TAU * 1.6 - t * 0.55 + l * 0.9 + lane * 0.7);
    float flutter = 0.06 * sin(t * (0.5 + hsh(cellId, 14.0) * 0.5) + hsh(cellId, 15.0) * TAU);
    float bank = wl_tanh(wave + (hsh(cellId, 16.0) - 0.5) * 0.5 + flutter);
    float pitch = (hsh(cellId, 17.0) - 0.5) * 0.24 + 0.2 * sin(s * TAU * 1.1 - t * 0.4 + l * 2.1);
    vec2 d2 = rb.T;
    vec2 w2 = vec2(-d2.y, d2.x);
    vec2 k2 = d2 * cos(yaw) + w2 * sin(yaw);
    vec2 j2 = vec2(-k2.y, k2.x);
    float cb = cos(bank), sb = sin(bank), cp = cos(pitch), sp = sin(pitch);
    vec3 kk = vec3(k2, 0.0);
    vec3 jj = vec3(j2, 0.0);
    vec3 m = vec3(0.0, 0.0, cb) + jj * sb;
    vec3 nrm = m * cp - kk * sp;
    vec3 axis = kk * cp + m * sp;
    vec3 side = jj * cb - vec3(0.0, 0.0, sb);

    // Pixel -> plate coordinates (orthographic inverse of the projection).
    vec2 A = axis.xy;
    vec2 S = side.xy;
    float det = A.x * S.y - A.y * S.x;
    det = sign(det) * max(abs(det), 0.05);
    vec2 x = vec2(P.x * S.y - P.y * S.x, A.x * P.y - A.y * P.x) / det / sizeS;

    float half_t = 0.06 + hsh(cellId, 18.0) * 0.04;
    float pxPerUnit = sizeS * halfH * sqrt(abs(det));
    float footprint = 0.7 / pxPerUnit;
    float sigmaU = sigmaPx / pxPerUnit;

    float dist = -1e9;
    float gap = 1e9;
    for (int e = 0; e < 5; e++) {
        if (float(e) >= n) break;
        vec2 a = V[e];
        vec2 ed = V[e + 1] - a;
        vec2 en = vec2(ed.y, -ed.x) / max(length(ed), 1e-4);
        float g = dot(x - a, en);
        dist = max(dist, g);
    }
    gap = -dist;
    float rim = half_t * length(nrm.xy);
    float dpx = (dist - rim) * pxPerUnit;
    float w = 0.5 + 1.7 * sigmaPx;
    float cov = smoothstep(w, -w, dpx);

    vec3 base = shard;

    if (cov <= 0.002) {
        // Bloom halo around the rims of shards next to the pixel.
        float hd = max(dpx, 0.0);
        halo += mix(mix(shard, accent, 0.5), vec3(1.0), 0.3) * exp(-hd / (5.0 + sigmaPx)) * 0.05 / (1.0 + sigmaPx * 0.4);
        return vec4(0.0);
    }

    // ---- Prism trace in the shard's local frame, as the original shader.
    float sizeW = sizeS * rb.d / (CAMERA * P_SCALE);
    vec3 center = vec3(qc, -CAMERA * P_SCALE) * (rb.d / (CAMERA * P_SCALE));
    vec3 o = vec3(dot(-center, axis), dot(-center, side), dot(-center, nrm)) / sizeW;
    vec3 rl = vec3(dot(rd, axis), dot(rd, side), dot(rd, nrm));

    float tNear = -1e9;
    float tFar = 1e9;
    vec3 hitN = vec3(0.0, 0.0, 1.0);
    for (int k = 0; k < 7; k++) {
        if (float(k) >= n + 2.0) break;
        vec3 pn;
        float pw;
        if (k == 0) { pn = vec3(0.0, 0.0, 1.0); pw = half_t; }
        else if (k == 1) { pn = vec3(0.0, 0.0, -1.0); pw = half_t; }
        else {
            vec2 a = V[0];
            vec2 b = V[1];
            for (int e = 0; e < 5; e++) { if (e == k - 2) { a = V[e]; b = V[e + 1]; } }
            vec2 ed = b - a;
            vec2 en = normalize(vec2(ed.y, -ed.x) + vec2(1e-6, 0.0));
            pn = vec3(en, 0.0);
            pw = dot(en, a);
        }
        float denom = dot(pn, rl);
        float gp = pw - dot(pn, o);
        if (abs(denom) < 1e-7) {
            if (gp < 0.0) tFar = -1e9;
        } else {
            float tt = gp / denom;
            if (denom < 0.0) {
                if (tt > tNear) { tNear = tt; hitN = pn; }
            } else if (tt < tFar) {
                tFar = tt;
            }
        }
    }
    float facing = o.z > 0.0 ? 1.0 : -1.0;
    float rz = abs(rl.z) < 1e-4 ? -1e-4 * facing : rl.z;
    float capT = (facing * half_t - o.z) / rz;
    bool missed = tNear > tFar;
    float hitT = missed ? capT : tNear;
    vec3 hn = missed ? vec3(0.0, 0.0, facing) : hitN;
    vec3 local = o + rl * hitT;

    float radius0 = max(half_t * 1.15, footprint * 2.5);
    float radius = radius0 + sigmaU * 1.5;
    float soften = radius0 / radius;
    vec3 nl = hn;
    float bevel;
    if (abs(nl.z) > 0.5) {
        float g0 = 1e9;
        for (int e = 0; e < 5; e++) {
            if (float(e) >= n) break;
            vec2 ed = V[e + 1] - V[e];
            vec2 en = vec2(ed.y, -ed.x) / max(length(ed), 1e-4);
            g0 = min(g0, dot(en, V[e]) - dot(en, local.xy));
        }
        vec2 toward = vec2(0.0);
        for (int e = 0; e < 5; e++) {
            if (float(e) >= n) break;
            vec2 ed = V[e + 1] - V[e];
            vec2 en = vec2(ed.y, -ed.x) / max(length(ed), 1e-4);
            float g = dot(en, V[e]) - dot(en, local.xy);
            toward += en * exp(-(g - g0) / (radius * 0.35));
        }
        toward = normalize(toward + vec2(1e-6, 0.0));
        bevel = clamp(1.0 - g0 / radius, 0.0, 1.0);
        float ang = bevel * bevel * 0.7853982;
        nl = normalize(nl * cos(ang) + vec3(toward, 0.0) * sin(ang));
    } else {
        float g0 = half_t - abs(local.z);
        bevel = clamp(1.0 - g0 / radius, 0.0, 1.0);
        float ang = bevel * bevel * 0.7853982;
        nl = normalize(nl * cos(ang) + vec3(0.0, 0.0, sign(local.z)) * sin(ang));
    }
    float bendRate = 1.8 / max(radius / footprint, 1.0);
    vec3 nb = normalize(axis * nl.x + side * nl.y + nrm * nl.z);
    float soft = bendRate * smoothstep(0.0, 0.25, bevel) * 1.2 + clamp(footprint * 0.6, 0.0, 0.5) + sigmaU * 0.3;

    vec3 view = -rd;
    vec3 refl = reflect(rd, nb);
    float ndv = clamp(dot(nb, view), 0.0, 1.0);
    // Tone ramps from accent (low) to shard colour (high) with world height.
    float worldY = qc.y * rb.d / (CAMERA * P_SCALE);
    vec3 tone = mix(accent, base, clamp(0.5 + worldY * 0.45, 0.0, 1.0));
    float tintR = hsh(cellId, 19.0);
    vec3 body = mix(tone, mix(base, accent, 0.5), tintR * 0.3);
    float wrap = clamp(dot(nb, KEY_DIR()) * 0.5 + 0.5, 0.0, 1.0);

    vec3 color;
    float alpha = 1.0;
    if (P_LIGHT_MODE == 1) {
        vec3 env = studio_light(refl, soft);
        if (P_MATERIAL == 1) {
            color = env * mix(body, vec3(1.0), 0.6);
        } else {
            float fres = schlick(ndv, 0.05);
            float diffuse = clamp(dot(nb, KEY_DIR()) * 0.5 + 0.5, 0.0, 1.0);
            alpha = clamp(0.32 + fres * 0.5 + (1.0 - abs(nl.z)) * 0.5, 0.0, 1.0);
            color = mix(body, vec3(1.0), 0.15) * (0.5 + 0.4 * diffuse) * alpha + (env - vec3(0.78)) * fres * 0.8;
        }
    } else if (P_MATERIAL == 1) {
        vec3 env = studio(refl, soft, accent, body);
        color = env * mix(body, vec3(1.0), 0.55) * 0.9 + body * 0.02;
    } else {
        float fres = schlick(ndv, 0.04);
        vec3 env = studio(refl, soft, accent, body);
        vec3 light = vec3(0.0);
        vec3 cursor = local;
        vec3 wr = refract(rd, nb, 1.0 / 1.5);
        vec3 dir = vec3(dot(wr, axis), dot(wr, side), dot(wr, nrm));
        float travelled = 0.0;
        float carry = 1.0 - fres;
        float through = 0.0;
        for (int bounce = 0; bounce < 3; bounce++) {
            float best = 1e9;
            vec3 outward = vec3(0.0, 0.0, -1.0);
            for (int k = 0; k < 7; k++) {
                if (float(k) >= n + 2.0) break;
                vec3 pn;
                float pw;
                if (k == 0) { pn = vec3(0.0, 0.0, 1.0); pw = half_t; }
                else if (k == 1) { pn = vec3(0.0, 0.0, -1.0); pw = half_t; }
                else {
                    vec2 a = V[0];
                    vec2 b = V[1];
                    for (int e = 0; e < 5; e++) { if (e == k - 2) { a = V[e]; b = V[e + 1]; } }
                    vec2 ed = b - a;
                    vec2 en = normalize(vec2(ed.y, -ed.x) + vec2(1e-6, 0.0));
                    pn = vec3(en, 0.0);
                    pw = dot(en, a);
                }
                float dn = dot(pn, dir);
                if (dn > 1e-6) {
                    float tt = (pw - dot(pn, cursor)) / dn;
                    if (tt < best) { best = tt; outward = pn; }
                }
            }
            best = max(best, 0.0);
            travelled += best;
            cursor += dir * best;
            vec3 escaped = refract(dir, -outward, 1.5);
            if (dot(escaped, escaped) > 0.0) {
                float leave = schlick(dot(dir, outward), 0.04);
                vec3 worldOut = normalize(axis * escaped.x + side * escaped.y + nrm * escaped.z);
                vec3 absorb = exp(-(vec3(1.0) - body) * travelled * sizeW * 22.0);
                float straight = smoothstep(0.97, 0.999, dot(worldOut, rd));
                float energy = carry * (1.0 - leave);
                light += studio(worldOut, soft * 2.0 + 0.04, accent, body) * absorb * energy * (1.0 - straight) * 2.0;
                through += energy * straight * (absorb.r + absorb.g + absorb.b) / 3.0;
                carry *= leave;
            }
            dir = reflect(dir, outward);
        }
        float edge = max(pow(1.0 - abs(nl.z), 1.2), bevel * bevel * 0.85) * soften;
        vec3 glow = mix(mix(body, accent, 0.35), vec3(1.0), 0.28) * edge * (0.35 + 0.45 * wrap);
        color = env * fres * 0.85 + light * (1.0 - smoothstep(0.0, 0.6, bevel)) + body * 0.02 + glow;
        alpha = clamp(1.0 - through + edge * 0.25, 0.0, 1.0);
    }

    color *= P_LIGHT_MODE == 1 ? 1.0 : 1.3;
    float haze = smoothstep(3.5, 12.0, rb.d) * (P_LIGHT_MODE == 1 ? 0.5 : 0.7);
    vec3 hazeColor = P_LIGHT_MODE == 1 ? mix(base, vec3(1.0), 0.72) : mix(base, accent, 0.5) * 0.3;
    color = mix(color, hazeColor * alpha + color * 0.3, haze);
    // Solid pixels (alpha >= 0.6) were written unpremultiplied in the
    // original, edge / translucent pixels premultiplied.
    float pm = alpha >= 0.6 ? 1.0 : alpha;
    return vec4(color * pm, alpha) * cov;
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float t = wl_time();
    float aspect = res.x / res.y;
    float halfH = res.y * 0.5;
    vec2 q = (frag - 0.5 * res) / halfH;
    vec3 shard = P_SHARD_COLOR;
    vec3 accent = P_ACCENT_COLOR;
    vec3 rd = normalize(vec3(q, -CAMERA * P_SCALE));

    vec4 scene = vec4(0.0);
    vec3 halo = vec3(0.0);
    float count = float(P_COUNT);

    for (int li = 0; li < 5; li++) {
        float l = 4.0 - float(li);
        float D = depth_of(l);

        if (P_MARKERS != 2) {
            vec4 mk = marker_layer(l, q, res, aspect, t, shard, accent);
            scene = mk + scene * (1.0 - mk.a);
        }

        Rib rb = ribbon(l, q, aspect, t);
        float sizeBase = CAMERA * 0.062 * P_SHARD_SIZE * growth_of(l) / D * P_SCALE;
        float hwBase = max(P_SPREAD * (0.15 + 0.035 * D) * CAMERA * P_SCALE / D, 0.03);
        // One shard per cell: n shards over the band area.
        float nShards = max(count * share_of(l), 8.0);
        float cell = sqrt(rb.len * 1.05 * 2.0 * hwBase / nShards);
        float dA = cell / 1.05;
        float nA = max(floor(rb.len / dA + 0.5), 1.0);
        // The ring is closed: a whole number of cells around it.
        if (P_PLACEMENT == 3) dA = rb.len / nA;
        float dC = cell;
        float reachMax = sizeBase * 1.85 * 1.1;
        float K = clamp(ceil(reachMax / (dA * rb.stretch) - 0.1), 1.0, 3.0);
        float limitA = (K + 0.1) * dA * rb.stretch;
        float limitC = 1.1 * dC;
        float bandHalf = rb.hw + min(reachMax, limitA) + 0.02;
        vec4 layer = vec4(0.0);
        vec3 layerHalo = vec3(0.0);
        if (abs(rb.c) < bandHalf) {
            float sigmaPx = min(P_DEPTH_OF_FIELD * res.y * 0.012 * abs(1.0 - 2.75 / D), res.y * 0.02);
            // Uniform along the whole ribbon (the original moves in arc length).
            float speedA = 0.09 * CAMERA * P_SCALE / D;
            float travel = t * speedA;
            float i0 = floor((rb.a - travel) / dA);
            float j0 = floor(rb.c / dC);
            vec2 N = vec2(-rb.T.y, rb.T.x);
            // Pass 1: find the two shards whose bounds are closest to the
            // pixel (more than two overlapping shards of one ribbon is rare).
            vec4 c0 = vec4(0.0, 0.0, 1e9, 0.0); // cellId, score, size
            vec4 c1 = vec4(0.0, 0.0, 1e9, 0.0);
            vec3 p0 = vec3(0.0);                  // P, lane
            vec3 p1 = vec3(0.0);
            vec2 b0 = vec2(0.0);                  // s, twist angle
            vec2 b1 = vec2(0.0);
            for (int di = -3; di <= 3; di++) {
                if (abs(float(di)) > K) continue;
                for (int dj = -1; dj <= 1; dj++) {
                    float ci = i0 + float(di);
                    float cj = j0 + float(dj);
                    float im = P_PLACEMENT == 3 ? mod(ci, nA) : ci;
                    vec2 cellId = vec2(im + l * 1013.0, cj + 64.0 + l * 37.0);
                    float ai = (ci + 0.5 + (hsh(cellId, 0.0) - 0.5) * 0.7) * dA + travel;
                    float cc = (cj + 0.5 + (hsh(cellId, 1.0) - 0.5) * 0.7) * dC;
                    float si = ai / rb.len;
                    if (P_PLACEMENT == 3) si = fract(si);
                    vec2 band = band_at(l, si, t);
                    float lane = cc / max(band.x, 1e-3);
                    if (abs(lane) > 1.0) continue;
                    float taper = P_PLACEMENT == 3 ? 1.0 : smoother(si / 0.12) * smoother((1.0 - si) / 0.12);
                    float sz = sizeBase * (0.6 + hsh(cellId, 2.0) * hsh(cellId, 3.0) * 1.25);
                    float core = 1.0 - 0.45 * lane * lane;
                    sz *= core * sqrt(taper) * smoothstep(1.0, 0.8, abs(lane));
                    sz = min(sz, min(limitA, limitC * 2.0) / 1.1);
                    if (sz < 1e-4) continue;
                    vec2 P = rb.T * ((rb.a - ai) * rb.stretch) + N * (rb.c - cc);
                    float r2 = sz * sz * 1.45 + 0.0003;
                    float score = dot(P, P) / r2;
                    if (score > 1.0) continue;
                    if (score < c0.z) {
                        c1 = c0;
                        p1 = p0;
                        b1 = b0;
                        c0 = vec4(cellId, score, sz);
                        p0 = vec3(P, lane);
                        b0 = vec2(si, band.y);
                    } else if (score < c1.z) {
                        c1 = vec4(cellId, score, sz);
                        p1 = vec3(P, lane);
                        b1 = vec2(si, band.y);
                    }
                }
            }
            // Pass 2: shade them, farther candidate first.
            for (int ci = 0; ci < 2; ci++) {
                vec4 cnd = ci == 0 ? c1 : c0;
                vec3 pp = ci == 0 ? p1 : p0;
                vec2 bb = ci == 0 ? b1 : b0;
                if (cnd.z > 1.0) continue;
                vec4 sh = shard_at(l, rb, bb, cnd.xy, pp.xy, q - pp.xy, cnd.w, pp.z, rd, res, sigmaPx, t, shard, accent, layerHalo);
                layer = sh + layer * (1.0 - sh.a);
            }
        }
        float fl = fade_of(l);
        scene = layer * fl + scene * (1.0 - layer.a * fl);
        halo += layerHalo * fl;
    }

    // Finish pass: exposure, grain, bloom halo, vignette.
    float noise = wl_hash12(floor(frag) + vec2(floor(t * 24.0), floor(t * 24.0) * 1.618)) - 0.5;
    float grain = 1.0 + noise * P_GRAIN;
    float alpha = clamp(scene.a, 0.0, 1.0);
    vec3 rgb;
    float a = alpha;
    if (P_LIGHT_MODE == 1) {
        rgb = clamp(scene.rgb, vec3(0.0), vec3(alpha)) * grain;
    } else {
        vec3 h = halo * P_GLOW * 2.5;
        rgb = (1.0 - exp(-max(scene.rgb, vec3(0.0)) * 1.3)) * grain + h / (1.0 + h);
        a = max(alpha, max(max(rgb.r, rgb.g), rgb.b));
    }
    if (P_VIGNETTE > 0.0) {
        float reach = max(P_VIGNETTE * 0.5 * min(res.x, res.y), 1.0);
        vec2 fromEdge = min(frag, res - frag);
        float mask = smoother(fromEdge.x / reach) * smoother(fromEdge.y / reach);
        rgb *= mask;
        a *= mask;
    }
    rgb = clamp(rgb, vec3(0.0), vec3(max(a, 0.0)));
    vec3 paper = P_BACKGROUND;
    if (P_LIGHT_MODE == 1) {
        float lumBg = dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722));
        paper = lumBg > 0.5 ? P_BACKGROUND : vec3(0.96, 0.96, 0.97);
    }
    gl_FragColor = vec4(clamp(wl_over(paper, vec4(rgb, a)), 0.0, 1.0), 1.0);
}
