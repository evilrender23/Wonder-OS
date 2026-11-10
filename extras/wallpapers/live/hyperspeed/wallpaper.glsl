// Hyperspeed — ported from React Bits <Hyperspeed /> (see ../LICENSE-REACTBITS.md).
// The three.js scene (bent road, instanced light-trail tubes, poles, dust
// lines, bloom) becomes one analytic pass: each pixel's ray is solved
// against the bent road surface, then the light trails of its own and the
// neighbouring lane (cars are dealt round-robin to lanes), the two nearest
// poles and a few depth slices of dust are evaluated in road coordinates.
// Bloom is an analytic halo around every trail. Boost, steering and the
// pointer are dropped.

#define PI 3.14159265358979
const float TRAVEL = 400.0;

float T;
vec3 B0;

float nsin(float v) { return sin(v) * 0.5 + 0.5; }

#if P_CURVE == 1
const float P0 = 0.02;
vec3 raw_bend(float p) {
    return vec3(cos(p * PI * 5.0 + T) * 25.0, sin(p * PI * 2.0 + PI * 0.5 + T) * 15.0, 0.0);
}
#elif P_CURVE == 2
const float P0 = 0.0125;
float windX(float p) { float c = cos(PI * p * 8.0 + T * 2.0); return cos(PI * p * 4.0 + T) * 25.0 + c * c * 5.0; }
float windY(float p) { float n = nsin(PI * p + T / 8.0); float n2 = n * n; return -nsin(PI * p * 8.0 + T) * 10.0 - n2 * n2 * n * 10.0; }
vec3 raw_bend(float p) { return vec3(windX(p), windY(p), 0.0); }
#elif P_CURVE == 3
const float P0 = 0.02;
vec3 raw_bend(float p) {
    return vec3(cos(p * PI * 3.0 + T) * 30.0, nsin(p * PI * 6.0 + T) * 30.0, nsin(p * PI * 10.0 + T) * 20.0);
}
#elif P_CURVE == 4
const float P0 = 0.0125;
vec3 raw_bend(float p) {
    return vec3(sin(p * PI * 2.0 + T) * 35.0, sin(p * PI * 3.0 + T) * 10.0, 0.0);
}
#elif P_CURVE == 5
const float P0 = 0.02;
float deepX(float p) { return sin(p * PI * 4.0 + T) * 10.0; }
float deepY(float p) { float q = p * 10.0; return q * q + sin(p * PI * 8.0 + T) * 20.0; }
vec3 raw_bend(float p) { return vec3(deepX(p), deepY(p), 0.0); }
#else
const float P0 = 0.0;
vec3 raw_bend(float p) { return vec3(0.0); }
#endif

vec3 bend(float p) {
    return (raw_bend(p) - B0) * P_CURVATURE;
}

vec3 look_shape() {
    float c = P_CURVATURE;
    float p = 0.025;
#if P_CURVE == 1
    vec3 g = raw_bend(p) - B0;
    return vec3(g.x * c * 2.0, g.y * c * 0.4, -3.0);
#elif P_CURVE == 2
    return vec3((windX(p) - windX(p + 0.007)) * c * -2.0, (windY(p) - windY(p + 0.007)) * c * -5.0, -10.0);
#elif P_CURVE == 3
    vec3 h = raw_bend(p) - B0;
    return vec3(h.x * c * 2.0, h.y * c * 2.0, h.z * c * 2.0 - 5.0);
#elif P_CURVE == 4
    vec3 r = raw_bend(p) - B0;
    return vec3(r.x * c, r.y * c, -5.0);
#elif P_CURVE == 5
    return vec3((deepX(p) - deepX(p + 0.01)) * c * -2.0, (deepY(p) - deepY(p + 0.01)) * c * -4.0, -10.0);
#else
    return vec3(0.0, 0.0, -10.0);
#endif
}

vec4 hash44(vec4 p4) {
    p4 = fract(p4 * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    p4 += dot(p4, p4.wzxy + 33.33);
    return fract((p4.xxyz + p4.yzzw) * p4.zywx);
}

vec3 tail_color(float tone) {
    float k = floor(tone * 3.0);
    return k < 0.5 ? P_TAIL_COLOR_1 : (k < 1.5 ? P_TAIL_COLOR_2 : P_TAIL_COLOR_3);
}

vec3 head_color(float tone) {
    float k = floor(tone * 3.0);
    return k < 0.5 ? P_HEAD_COLOR_1 : (k < 1.5 ? P_HEAD_COLOR_2 : P_HEAD_COLOR_3);
}

// Ray / road solve output.
float g_s;     // ray parameter of the road hit
float g_Z;     // road z (object space, before bend)
float g_X;     // road x (object space, before bend)
float g_k;     // d(height above road)/d(road z) along the ray

float road_f(vec3 eye, vec3 dir, float zr, out float s) {
    vec3 b = bend(abs(zr) / TRAVEL);
    s = (zr + b.z - eye.z) / dir.z;
    return eye.y + s * dir.y - b.y;
}

bool solve_road(vec3 eye, vec3 dir) {
    if (dir.z > -1e-4) return false;
    float s;
    float za = eye.z - 0.05;
    float fa = road_f(eye, dir, za, s);
    float zb = za;
    float fb = fa;
    bool found = false;
    for (int k = 1; k <= 10; k++) {
        float q = float(k) / 10.0;
        zb = eye.z - 0.05 - (TRAVEL + eye.z) * q * q;
        fb = road_f(eye, dir, zb, s);
        if (fb < 0.0) { found = true; break; }
        za = zb;
        fa = fb;
    }
    if (!found) return false;
    // Illinois false position: no one-sided stagnation.
    float sidef = 0.0;
    for (int k = 0; k < 7; k++) {
        float zm = (za * fb - zb * fa) / (fb - fa);
        float fm = road_f(eye, dir, zm, s);
        if (fm > 0.0) {
            za = zm; fa = fm;
            if (sidef > 0.5) fb *= 0.5;
            sidef = 1.0;
        } else {
            zb = zm; fb = fm;
            if (sidef < -0.5) fa *= 0.5;
            sidef = -1.0;
        }
    }
    float z = (za * fb - zb * fa) / (fb - fa);
    float delta = 0.002 * abs(z) + 0.02;
    float s1;
    float f1 = road_f(eye, dir, z + delta, s1);
    float f0 = road_f(eye, dir, z - delta, s1);
    road_f(eye, dir, z, s);
    g_s = s;
    g_Z = z;
    g_k = (f1 - f0) / (2.0 * delta);
    vec3 b = bend(abs(z) / TRAVEL);
    g_X = eye.x + s * dir.x - b.x;
    return true;
}

float g_aa;

float stripe(float d, float w) {
    float aa = g_aa;
    return 1.0 - smoothstep(w - aa, w + aa, d);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    T = wl_time();
    B0 = raw_bend(P0);

#if P_LIGHT_MODE
    const bool LIGHT = true;
    vec3 road_color = vec3(0.937, 0.937, 0.949);
    vec3 line_color = vec3(0.816, 0.816, 0.843);
    vec3 dust_color = vec3(0.094, 0.094, 0.106);
#else
    const bool LIGHT = false;
    vec3 road_color = P_ROAD_COLOR;
    vec3 line_color = P_LINE_COLOR;
    vec3 dust_color = vec3(1.0);
#endif

    float rw = max(P_ROAD_WIDTH, 1.0);
    float median = max(P_MEDIAN_WIDTH, 0.0);
    float lanes = float(P_LANES);
    float lw = rw / lanes;
    float halfRoad = rw * 0.5 + median * 0.5;
    float surface = 0.45 + 0.55 * P_ROAD_OPACITY;
    float fog_near = TRAVEL * 0.22;
    float fog_far = TRAVEL * 0.95;

    // Camera (three.js PerspectiveCamera, vertical fov, lookAt with +y up).
    vec3 eye = vec3(0.0, 8.0, -5.0);
    vec3 fwd = normalize(look_shape());
    vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, fwd);
    float fov = clamp(P_FOV, 20.0, 170.0);
    float tanHalf = tan(fov * PI / 360.0);
    float aspect = res.x / res.y;
    vec2 sc = frag / res * 2.0 - 1.0;
    vec3 dir = normalize(fwd + sc.x * tanHalf * aspect * right + sc.y * tanHalf * up);
    float pixel = 2.0 * tanHalf / res.y;   // world size of a pixel at depth 1
    float res_scale = res.y / 1080.0;

    vec3 col = P_BACKGROUND;
    vec3 add = vec3(0.0);
    vec3 halo = vec3(0.0);

    if (solve_road(eye, dir)) {
        float vz = max(0.1, g_s * dot(dir, fwd));
        float pxw = vz * pixel;
        float fog = smoothstep(fog_near, fog_far, vz);
        float X = g_X;
        float Z = g_Z;
        float k = g_k;
        float rx = dir.x / dir.z;          // dX / dZ along the ray
        float kk = abs(k);
        float tube_den = sqrt(rx * rx + k * k);
        float sin_len = sqrt(rx * rx + k * k) / sqrt(rx * rx + k * k + 1.0);

        // ---- road ----
        g_aa = 1.2 * pxw;
        float roadHalf = (rw * 2.0 + median + 3.0) * 0.5;
        if (abs(X) < roadHalf && Z > -TRAVEL && Z < 0.0) {
            float rxd = abs(X) - median * 0.5;
            vec3 rc = rxd < 0.0 ? mix(road_color, line_color, 0.12) : road_color;
            float shoulder = max(stripe(abs(rxd - 0.3), 0.07), stripe(abs(rxd - (rw - 0.3)), 0.07));
            float nearest = floor(rxd / lw + 0.5);
            float inner = step(0.5, nearest) * step(nearest, lanes - 0.5);
            float dash = step(0.5, fract((Z - T * 120.0) / 14.0));
            float marks = max(shoulder, stripe(abs(rxd - nearest * lw), 0.06) * inner * dash);
            if (rxd >= 0.0) rc = mix(rc, line_color, marks);
            float ra = (1.0 - fog) * clamp(P_ROAD_OPACITY, 0.0, 1.0);
            col = mix(col, rc, ra);
        }

        // ---- light trails ----
        float glow_on = P_GLOW > 0.0 ? 1.0 : 0.0;
        float R1 = 5.0 * res_scale;
        float R2 = 20.0 * res_scale;
        float side = X < 0.0 ? 0.0 : 1.0;
        float dirSign = side < 0.5 ? 1.0 : -1.0;
        float roadX = side < 0.5 ? -halfRoad : halfRoad;
        vec2 speedRange = side < 0.5 ? vec2(60.0, 80.0) : vec2(120.0, 160.0);
        float xl = (X - roadX + rw * 0.5) / lw;
        float lane0 = clamp(floor(xl), 0.0, lanes - 1.0);
        float lane1 = lane0 + (fract(xl) > 0.5 ? 1.0 : -1.0);
        if (xl < 0.0) lane1 = -1.0;
        if (xl >= lanes) lane1 = -1.0;
        float N = float(P_DENSITY);
        float trail = max(P_TRAIL_LENGTH, 0.05);
        float size = max(P_LIGHT_SIZE, 0.1);
        float core_int = LIGHT ? 0.95 : 1.0;
        float core_white = LIGHT ? 0.0 : 0.75;
        float refl_int = P_REFLECTIONS * (LIGHT ? 0.22 : 0.42) * surface;

        for (int li = 0; li < 2; li++) {
            float lane = li == 0 ? lane0 : lane1;
            if (lane < 0.0 || lane > lanes - 0.5) continue;
            for (int j = 0; j < 120; j++) {
                float ci = lane + lanes * float(j);
                if (ci >= N) break;
                vec4 a = hash44(vec4(ci, side, 3.7, 11.3));
                vec4 b = hash44(vec4(ci, side, 19.1, 5.9));
                float tone = fract(a.x * 7.13 + b.w * 3.31);
                float bw = b.w * fract(a.y * 13.7 + b.z * 5.1);
                float laneX = (lane + 0.5) * lw - rw * 0.5 + (a.y - 0.5) * 0.5 * lw;
                float carWidth = mix(0.3, 0.5, a.z) * lw;
                float radius = mix(0.05, 0.14, a.w) * size;
                float len = mix(0.03, 0.2, b.x) * TRAVEL * trail;
                float speed = mix(speedRange.x, speedRange.y, b.y) * dirSign;
                float m = mod(T * speed + b.z * TRAVEL, TRAVEL);
                float zNear = len - m;
                float zFar = -m;
                float cy = bw * 0.8 + radius * 1.3;
                float cx = roadX + laneX;
                vec3 color = side < 0.5 ? tail_color(tone) : head_color(tone);
                vec3 hot = mix(color, vec3(1.0), core_white);

                // Core tube at height cy, then the mirrored reflection at -1.8 cy.
                for (int layer = 0; layer < 2; layer++) {
                    float h = layer == 0 ? cy : -1.8 * cy;
                    float dz = h / k;
                    float Zh = Z + dz;
                    float Xh = X + rx * dz;
                    float margin = (layer == 0 ? 1.0 : 2.0) * (2.0 * R2 * pxw / max(sin_len, 0.05));
                    if (Zh < zFar - margin || Zh > zNear + margin) continue;
                    float uvx = clamp((zNear - Zh) / len, 0.0, 1.0);
                    float fade = side < 0.5 ? smoothstep(0.0, 0.6, uvx) : smoothstep(1.0, 0.4, uvx);
                    float outside = max(max(Zh - zNear, zFar - Zh), 0.0) * sin_len / pxw;
                    float ends = clamp(1.0 - outside, 0.0, 1.0);
                    float rscale = layer == 0 ? 1.0 : 2.1;
                    float ky = layer == 0 ? k : k / 2.2;
                    float wanted = radius * rscale;
                    float shown = max(wanted, pxw * 0.75);
                    float thin = wanted / shown;
                    float sharp = layer == 0 ? 0.85 : 1.2;
                    float fogl = 1.0 - fog;
                    for (int pr = 0; pr < 2; pr++) {
                        float lx = cx + (pr == 0 ? -0.5 : 0.5) * carWidth;
                        float d = abs(Xh - lx) * abs(ky) / sqrt(rx * rx + ky * ky);
                        float shownAA = shown + 0.35 * pxw;
                        float q = d / shownAA;
                        float facing = sqrt(max(1.0 - q * q, 0.0));
                        float energy = fade * thin * fogl * (shown / shownAA);
                        if (layer == 0) {
                            float alpha = pow(facing, sharp) * energy * ends * core_int;
                            vec3 c = mix(color, vec3(1.0), pow(facing, 12.0) * core_white);
                            if (LIGHT) col = mix(col, c, clamp(alpha, 0.0, 1.0));
                            else add += c * min(alpha, 1.0);
                            if (glow_on > 0.0) {
                                float dpx = length(vec2(d / pxw, outside));
                                float w = 2.0 * shown / pxw;
                                float hk = w * (0.32 * exp(-dpx * dpx / (R1 * R1)) / R1 + 0.22 * exp(-dpx * dpx / (R2 * R2)) / R2);
                                halo += hot * hk * fade * thin * fogl;
                            }
                        } else {
                            float alpha = pow(facing, sharp) * energy * ends * refl_int * exp(-cy * 0.9);
                            if (LIGHT) col = mix(col, color, clamp(alpha, 0.0, 1.0));
                            else add += color * min(alpha, 1.0);
                        }
                    }
                }
            }
        }

        // ---- poles ----
        if (P_POLES > 0) {
            float count = float(P_POLES);
            float sp = TRAVEL / count;
            float edge = rw + median * 0.5 + 0.8;
            float pole_core_int = LIGHT ? 0.9 : 1.0;
            float pole_white = LIGHT ? 0.0 : 0.6;
            float pole_refl = P_REFLECTIONS * (LIGHT ? 0.2 : 0.38) * surface;
            float hmax = 1.9 * max(P_POLE_HEIGHT, 0.1);
            float lat = abs(rx) / sqrt(rx * rx + 1.0);
            for (int ps = 0; ps < 2; ps++) {
                float sgn = ps == 0 ? -1.0 : 1.0;
                float s01 = float(ps);
                if (abs(rx) < 1e-5) continue;
                float dzc = (sgn * edge - X) / rx;
                float h = k * dzc;
                float hpad = 2.5 * R2 * pxw * glow_on;
                if (h > hmax + hpad || h < -1.6 * hmax) continue;
                float Zc = Z + dzc;
                if (Zc < -TRAVEL || Zc > 0.0) continue;
                float w = mod(Zc + TRAVEL - T * 120.0, TRAVEL);
                float i0 = floor(w / sp - 0.5 * s01);
                for (int c = 0; c < 2; c++) {
                    float ii = mod(i0 - float(c), count);
                    vec4 s = hash44(vec4(ii, s01, 41.3, 7.7));
                    float width = mix(0.08, 0.22, s.y);
                    float height = mix(1.3, 1.9, s.z) * max(P_POLE_HEIGHT, 0.1);
                    float zp = -TRAVEL + mod(T * 120.0 + (ii + 0.5 * s01 + s.x * 0.6) * sp, TRAVEL);
                    vec3 pc = P_POLE_COLOR;
                    bool mirror = h < 0.0;
                    float above = mirror ? -h / 1.6 : h;
                    float vH = above / height;
                    float over = max(above - height, 0.0) / pxw;
                    if (mirror && vH > 1.0) continue;
                    float ends = smoothstep(0.0, 0.08, vH) * smoothstep(1.0, 0.9, vH);
                    float wanted = width * (mirror ? 2.4 : 1.0);
                    float shown = max(wanted, pxw * 0.75);
                    float shownAA = shown + 0.35 * pxw;
                    float d = abs(Zc - zp) * lat;
                    float q = d / shownAA;
                    float facing = sqrt(max(1.0 - q * q, 0.0));
                    float energy = wanted / shownAA * (1.0 - fog) * ends;
                    if (!mirror) {
                        float alpha = pow(facing, 0.8) * energy * pole_core_int;
                        vec3 c2 = mix(pc, vec3(1.0), pow(facing, 12.0) * pole_white);
                        if (LIGHT) col = mix(col, c2, clamp(alpha, 0.0, 1.0));
                        else add += c2 * min(alpha, 1.0);
                        if (glow_on > 0.0) {
                            float dpx = length(vec2(d / pxw, over));
                            float wp = 2.0 * shown / pxw;
                            float hk = wp * (0.32 * exp(-dpx * dpx / (R1 * R1)) / R1 + 0.22 * exp(-dpx * dpx / (R2 * R2)) / R2);
                            float vends = smoothstep(0.0, 0.08, vH);
                            halo += mix(pc, vec3(1.0), pole_white) * hk * vends * (1.0 - fog);
                        }
                    } else {
                        float alpha = pow(facing, 1.2) * energy * pole_refl * exp(-above * 0.6);
                        if (LIGHT) col = mix(col, pc, clamp(alpha, 0.0, 1.0));
                        else add += pc * min(alpha, 1.0);
                    }
                }
            }
        }
    }

    // ---- dust: depth slices of sparse 1px streaks flying at the camera ----
    if (P_DUST > 0 && dir.z < -1e-3) {
        const float SLICES = 12.0;
        float spread = (rw * 2.0 + median) * 5.0;
        vec2 cell = vec2(10.0, 6.0);
        float cells = (spread / cell.x) * (41.0 / cell.y);
        float q = float(P_DUST) / SLICES / cells;
        float dust_int = LIGHT ? 0.35 : 0.45;
        for (int sl = 0; sl < 12; sl++) {
            float fk = float(sl);
            vec2 sh = fract(sin(vec2(fk * 12.9898 + 1.3, fk * 78.233 + 4.1)) * 43758.5453);
            float speed = mix(140.0, 260.0, sh.x);
            float zk = -TRAVEL + mod(T * speed + sh.y * TRAVEL, TRAVEL);
            if (zk > eye.z - 0.5) continue;
            vec3 bb = bend(abs(zk) / TRAVEL);
            float s = (zk + bb.z - eye.z) / dir.z;
            vec3 W = eye + s * dir;
            vec2 local = vec2(W.x - bb.x, W.y - bb.y - 1.0);
            if (local.y < 0.0 || local.y > 41.0 || abs(local.x) > spread * 0.5) continue;
            vec2 id = floor(local / cell);
            vec4 r = hash44(vec4(id, fk, 23.1));
            if (r.x > q) continue;
            vec2 ppos = (id + 0.1 + 0.8 * r.yz) * cell;
            float len = 0.6 * mix(0.6, 1.4, r.w);
            vec3 P1 = vec3(ppos.x + bb.x, ppos.y + 1.0 + bb.y, zk + bb.z);
            vec3 P2 = P1 + vec3(0.0, 0.0, len);
            vec3 v1 = P1 - eye;
            vec3 v2 = P2 - eye;
            float z1 = dot(v1, fwd);
            float z2 = dot(v2, fwd);
            if (z2 < 0.1) continue;
            vec2 s1 = vec2(dot(v1, right), dot(v1, up)) / (z1 * pixel);
            vec2 s2 = vec2(dot(v2, right), dot(v2, up)) / (z2 * pixel);
            vec2 pf = (frag - 0.5 * res);
            vec2 ab = s2 - s1;
            float tt = clamp(dot(pf - s1, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
            float dd = length(pf - s1 - ab * tt);
            float cov = clamp(1.0 - dd, 0.0, 1.0);
            float a = cov * tt * (1.0 - smoothstep(fog_near, fog_far, z2)) * dust_int;
            if (LIGHT) col = mix(col, dust_color, a);
            else add += dust_color * a;
        }
    }

    if (P_GLOW > 0.0) {
        float strength = P_GLOW * (LIGHT ? 0.5 : 1.4);
        float ceiling = P_GLOW * (LIGHT ? 0.3 : 0.5);
        vec3 hc = halo * strength;
        float peak = max(max(hc.r, max(hc.g, hc.b)), 1e-4);
        hc *= ceiling * (1.0 - exp(-peak / ceiling)) / peak;
        if (LIGHT) col = mix(col, hc / max(peak, 1e-4) * 0.8, clamp(peak, 0.0, 1.0) * 0.4);
        else add += hc;
    }

    gl_FragColor = vec4(clamp(col + add, 0.0, 1.0), 1.0);
}
