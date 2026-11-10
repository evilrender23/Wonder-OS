// GhostFibers — ported from React Bits <GhostFibers /> (see ../LICENSE-REACTBITS.md).
// The component draws up to 3 x 180 x 780 instanced dots along twisted 3D
// ribbons, with depth-of-field bokeh, then a 5-level bloom. Single pass:
// for each ribbon the pixel is located on the projected centre line (screen
// Newton steps), the ribbon's cross-section there gives each layer's fibres
// as parallel screen lines, and the three fibres nearest the pixel (and the
// two nearest dots on each) are evaluated exactly. Where dots or fibres
// overlap more than that (folds, heavy blur) the same sum is taken as a
// continuous density. Bloom is a widened density, dust is a jittered grid.
// Pointer focus/part, click pulses and the intro are dropped.

#define PI 3.14159265
const float CAMERA = 7.5;
const float FOCAL = 1.7388;       // 1 / (2 tan(FOV / 2)), FOV 0.56
const float RIBBON_LENGTH = 24.0;

vec2 g_res;
float g_t;
float g_K;            // FOCAL * height: pixels per unit at depth 1
float g_scale;        // scale * fit
mat3 g_orient;
vec4 g_place;
float g_dpr;

float hash11(float n) {
    return fract(sin(n * 91.3458) * 47453.5453);
}

vec4 placement(int r) {
    if (r == 1) return vec4(1.9, -4.6, -0.08, 2.4);
    if (r == 2) return vec4(-1.05, 2.0, 0.12, 4.1);
    return vec4(0.0);
}

vec3 center_local(float s) {
    float ph = g_place.w;
    float x = s * RIBBON_LENGTH;
    float y = P_WAVES * (0.52 * sin(x * 0.43 + g_t * 0.31 + ph) + 0.26 * sin(x * 0.79 - g_t * 0.23 + 1.3 + ph * 0.7))
        + g_place.x + x * g_place.z;
    float z = P_WAVES * 0.65 * sin(x * 0.33 + g_t * 0.19 + 0.7 + ph * 1.3) + g_place.y;
    return vec3(x, y, z);
}

vec3 to_world(vec3 local) {
    return g_orient * (local * g_scale);
}

vec3 project(vec3 w) {
    float depth = CAMERA - w.z;
    return vec3(g_res * 0.5 + g_K * w.xy / max(depth, 0.05), depth);
}

// Bokeh/core dot profile of the component's fragment shader.
float dot_shape(float rho, float soft, float blur) {
    if (rho >= 1.0) return 0.0;
    float disk = 1.0 - smoothstep(1.0 - soft, 1.0, rho);
    float bokeh = disk * (0.94 + 0.08 * smoothstep(0.55, 0.95, rho));
    float core = exp(-rho * rho * 3.2);
    return mix(core, bokeh, blur);
}

// Logistic stand-in for the normal CDF.
float cdf(float x) {
    return 1.0 / (1.0 + exp(-1.702 * clamp(x, -20.0, 20.0)));
}

vec3 tint_color(float tint, float highlight) {
    return mix(mix(P_GLOW_COLOR, P_SECONDARY_COLOR, tint), P_LINE_COLOR, highlight);
}

float style_length() {
#if P_FIBER_STYLE == 1
    return 0.45;
#elif P_FIBER_STYLE == 2
    return 1.0;
#else
    return 0.0;
#endif
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    g_res = res;
    g_dpr = max(1.0, res.y / 1440.0);
    float aspect = res.x / res.y;
    float fit = clamp(aspect / 1.5, 0.6, 1.0);
    g_scale = P_SCALE * fit;
    g_K = FOCAL * res.y;

    // The component's clocks: time at speed 0.5, roll at speed x rollSpeed,
    // dots flowing at flow x 12 dots per second.
    float clock = wl_time();
    g_t = clock * 0.5;
    float roll = g_t * P_ROLL_SPEED;
    float flow = clock * P_FLOW * 12.0;

    float yaw = P_PERSPECTIVE * 0.85;
    float pitch = 0.12;
    float rz = radians(P_ROTATION);
    float cy = cos(yaw), sy = sin(yaw), cp = cos(pitch), sp = sin(pitch), cr = cos(rz), sr = sin(rz);
    mat3 ry = mat3(cy, 0.0, -sy, 0.0, 1.0, 0.0, sy, 0.0, cy);
    mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, cp, sp, 0.0, -sp, cp);
    mat3 rzm = mat3(cr, sr, 0.0, -sr, cr, 0.0, 0.0, 0.0, 1.0);
    g_orient = rzm * rx * ry;

    float threads = float(P_THREADS);
    float per_layer = max(floor(threads / 3.0), 1.0);
    float dots = floor(260.0 * P_DENSITY + 0.5);
    float layers = float(P_LAYERS);
    float blur_cap = 22.0 * clamp(sqrt(56000.0 / (threads * dots * layers)), 0.45, 1.0);
    float area = pow(max(0.8 * P_THICKNESS, 0.45), 2.0) / 0.64;
    float load = pow((threads / 72.0) * P_DENSITY * area, -0.75);
    float size = 1.6 * P_THICKNESS;
    float width = 0.62 * P_SPREAD;
    float focus = CAMERA + P_FOCUS_DEPTH * 3.0;
    float dl = style_length();
    vec3 eye = vec3(0.0, 0.0, CAMERA);

    vec3 ray = normalize(vec3((frag - res * 0.5) / g_K, -1.0));

    vec4 scene = vec4(0.0);   // premultiplied colour, coverage
    vec3 bloom = vec3(0.0);

    for (int ribbon = 0; ribbon < 3; ribbon++) {
        if (ribbon >= P_LAYERS) break;
        g_place = placement(ribbon);
        float rib = float(ribbon);
        float gain = (ribbon == 0 ? 1.0 : (ribbon == 1 ? 0.5 : 0.55)) * load;
        float rib_tint = ribbon == 0 ? 0.0 : (ribbon == 1 ? 0.75 : 0.35);

        // Start from the closest approach between the pixel ray and the
        // ribbon's straight axis, keeping the part in front of the camera.
        vec3 O = to_world(vec3(0.0, g_place.x, g_place.y));
        vec3 U = g_orient * (vec3(1.0, g_place.z, 0.0) * g_scale);
        vec3 w0 = O - eye;
        float a = dot(U, U), b = dot(U, ray), d = dot(U, w0), e = dot(ray, w0);
        float x0 = (b * e - d) / max(a - b * b, 1e-5);
        float s_lo = -0.5, s_hi = 0.5;
        if (abs(U.z) > 1e-4) {
            float x_lim = (CAMERA - 1.2 - O.z) / U.z / RIBBON_LENGTH;
            if (U.z > 0.0) s_hi = min(s_hi, x_lim); else s_lo = max(s_lo, x_lim);
        }
        if (s_lo >= s_hi) continue;
        float s = clamp(x0 / RIBBON_LENGTH, s_lo, s_hi);

        vec3 c0, c1, P, P1;
        vec2 T;
        for (int k = 0; k < 3; k++) {
            c0 = center_local(s);
            c1 = center_local(s + 0.004);
            P = project(to_world(c0));
            P1 = project(to_world(c1));
            T = (P1.xy - P.xy) / 0.004;
            if (k < 2) s = clamp(s + dot(frag - P.xy, T) / max(dot(T, T), 1e-4), s_lo, s_hi);
        }
        float depth = P.z;
        if (depth < 0.3) continue;
        float t_len = max(length(T), 1e-3);
        vec2 th = T / t_len;
        vec2 ns = vec2(-th.y, th.x);
        vec2 rel = frag - P.xy;
        float delta = dot(rel, ns);
        // Beyond a clamped end (ribbon tip or the camera plane) there is
        // nothing to draw: the nearest point is not abeam of the pixel.
        if (abs(dot(rel, th)) > 0.08 * t_len) continue;

        // Dot size, defocus and the energy that spreads over the disc.
        float base_r = max(0.5 * size * g_dpr * CAMERA / max(depth, 0.1), 0.45);
        float coc = min(P_DEPTH_OF_FIELD * abs(1.0 - focus / max(depth, 0.1)) * res.y * 0.035, blur_cap * g_dpr);
        float radius = base_r + coc;
        float spill = max(10.0 * g_dpr, base_r);
        float energy = pow(base_r / min(radius, spill), 1.25) * pow(min(1.0, spill / radius), 2.0);
        // Sub-pixel dots: evaluate on a 1px footprint, same energy.
        float r_eval = max(radius, 1.0);
        energy *= (radius * radius) / (r_eval * r_eval);
        radius = r_eval;
        float blur = smoothstep(0.25, 0.7, coc / radius);
        float soft = clamp(2.6 / radius, 0.14, 1.0);

        float focal_px = res.y * FOCAL / max(depth, 0.1);
        float half_px = width * g_scale * focal_px;
        float band = half_px * 1.4 + radius;
        float bloom_r = res.y * 0.03 + radius;
        if (abs(delta) > band + bloom_r * 3.0) continue;

        vec3 cw = to_world(c0);
        float sp_along = t_len / dots;
        float spacing_face = length(to_world(c1) - cw) / 0.004 * focal_px / dots;
        float trail = (sp_along + 2.0 * base_r) / (spacing_face + 2.0 * base_r);
        float end_fade = smoothstep(0.5, 0.4, abs(s)) * smoothstep(1.6, 4.2, depth);
        float depth_tint = clamp(rib_tint + smoothstep(focus, focus + 5.0, depth) * 0.5, 0.0, 1.0);
        float seg_px = dl * sp_along;
        float seg_gain = mix(1.0, (2.0 * radius) / (seg_px + 2.0 * radius) * 1.6, dl);
        // Integrals of the dot profile: area (in r^2) and cross-section (in r).
        float area_k = mix(0.94, 3.05, blur);
        float width_k = mix(0.98, 1.9, blur);
        float sigma_k = mix(0.395, 0.5, blur);
        float dot_area = radius * radius * area_k + seg_px * radius * width_k;
        float w_along = smoothstep(0.8, 1.6, radius / (sp_along * (1.0 + 2.0 * dl)));

        // Ribbon frame at s.
        vec3 tang = normalize(c1 - c0);
        vec3 n0 = normalize(vec3(0.0, 1.0, 0.0) - tang * tang.y);
        vec3 b0 = cross(tang, n0);
        float xs = s * RIBBON_LENGTH;
        float th0 = P_TWIST * 0.483 * xs + roll + g_place.w * 0.6 + 0.5 * sin(xs * 0.26 + g_t * 0.27 + g_place.w);
        float hw = width * (0.78 + 0.22 * cos(s * PI));
        vec3 to_eye = normalize(eye - cw);

        for (int li = 0; li < 3; li++) {
            float layer = float(li) - 1.0;
            float ang = th0 + layer * P_VOLUME * 0.42;
            vec3 across = cos(ang) * n0 + sin(ang) * b0;
            vec3 sheet = -sin(ang) * n0 + cos(ang) * b0;
            vec3 aw = g_orient * (across * hw * g_scale);
            vec3 bw = g_orient * (sheet * hw * g_scale);
            // Screen-space offsets (linearised projection around the centre).
            vec2 as2 = g_K / depth * (aw.xy + cw.xy / depth * aw.z);
            vec2 bs2 = g_K / depth * (bw.xy + cw.xy / depth * bw.z);
            float pa = dot(as2, ns);
            float pb = dot(bs2, ns);
            float facing = abs(dot(normalize(g_orient * sheet), to_eye));
            float fold = pow(1.0 - facing, 6.0) * P_FOLD_GLOW;
            float stack = (2.0 * half_px * facing + 2.0 * base_r) / (2.0 * half_px + 2.0 * base_r);
            float crowd = sqrt(clamp(stack * trail, 0.002, 1.0));
            float lw = mix(1.0, 0.72, abs(layer) * P_VOLUME);
            float i_layer = energy * crowd * lw * 0.8 * gain * seg_gain;
            float m_layer = layer * P_VOLUME * 0.16;
            float sp_perp = max(abs(pa) * 2.0 / per_layer, 1e-3);
            float w_perp = smoothstep(1.0, 2.0, radius / sp_perp);
            float centre = m_layer * pb;

            // Continuous density of this layer (all fibres, all dots).
            float lo = centre - abs(pa);
            float hi = centre + abs(pa);
            float sig = radius * sigma_k;
            float cover = cdf((delta - lo) / sig) - cdf((delta - hi) / sig);
            float v_here = clamp((delta - centre) / (abs(pa) > 1e-3 ? pa : 1e-3), -1.0, 1.0);
            float tint_c = P_GRADIENT == 0 ? depth_tint : (P_GRADIENT == 1 ? smoothstep(-0.42, 0.42, s) : v_here * 0.5 + 0.5);
            float dens = i_layer * 0.725 * end_fade * (1.0 + fold * 7.0) * dot_area / (sp_along * sp_perp);
            vec3 col_c = tint_color(tint_c, clamp(fold * 0.55, 0.0, 1.0));

            // Bloom: the same density spread wide, mostly from folds.
            float sig_b = bloom_r;
            float cover_b = cdf((delta - lo) / sig_b) - cdf((delta - hi) / sig_b);
            float dens_b = i_layer * 0.725 * end_fade * (0.25 + fold * 7.0) * dot_area / (sp_along * sp_perp);
            bloom += col_c * min(dens_b * cover_b, 4.0);

            if (w_perp > 0.999) {
                float c = dens * cover;
                scene += vec4(col_c * c, c);
                continue;
            }

            // Discrete: the three fibres nearest the pixel.
            vec4 disc = vec4(0.0);
            float v_star = (delta - centre) / (abs(pa) > 1e-4 ? pa : 1e-4);
            float slot0 = floor((v_star + 1.0) * 0.5 * per_layer);
            for (int ci = 0; ci < 3; ci++) {
                float slot = slot0 + float(ci) - 1.0;
                if (slot < 0.0 || slot >= per_layer) continue;
                float fi = slot * 3.0 + layer + 1.0;
                if (fi >= threads) continue;
                // Cheap reject before hashing: the slot's nominal position
                // plus the largest jitter must come within a dot radius.
                float v_nom = ((slot + 0.5) / per_layer) * 2.0 - 1.0;
                if (abs(delta - centre - v_nom * pa) > radius + 0.45 * sp_perp + abs(pb) * 0.06 + 1.0) continue;
                float seed = hash11(fi * 1.731 + rib * 17.0);
                float v = ((slot + 0.5) / per_layer) * 2.0 - 1.0 + (hash11(fi * 3.113 + 4.0) - 0.5) * (1.6 / per_layer)
                    + layer * 0.12 / per_layer;
                float d_off = (hash11(fi * 7.919 + 2.0) - 0.5) * 0.12 * (0.35 + 6.0 * s * s);
                vec3 fw = cw + v * aw + (m_layer + d_off) * bw;
                vec3 F = project(fw);
                vec2 rf = frag - F.xy;
                float perp = dot(rf, ns);
                if (abs(perp) > radius) continue;
                float along = dot(rf, th);
                float s_k = s + along / t_len;
                if (abs(s_k) > 0.5) continue;
                float tip_end = mix(0.6, 0.06 + 0.26 * hash11(fi * 9.137 + g_place.w * 3.0), P_TIPS);
                float tip = exp(-pow((s_k - tip_end) / 0.0065, 2.0)) * P_TIPS;
                float fade = smoothstep(0.5, 0.4, abs(s_k)) * smoothstep(1.6, 4.2, depth)
                    * smoothstep(tip_end + 0.003, tip_end - 0.001, s_k);
                float strength = mix(0.45, 1.0, hash11(fi * 5.37 + 1.0));
                float tint_k = P_GRADIENT == 0 ? depth_tint : (P_GRADIENT == 1 ? smoothstep(-0.42, 0.42, s_k) : clamp(v * 0.5 + 0.5, 0.0, 1.0));
                float base_i = i_layer * strength * fade;

                // The two dots (or dash segments) either side of the pixel.
                float u = (s_k + 0.5) * dots - flow - seed * dots;
                float j0 = floor(u);
                vec4 dsum = vec4(0.0);
                for (int jj = 0; jj < 2; jj++) {
                    float j = j0 + float(jj);
                    float along_px = (u - j) * sp_along;
                    float ac = clamp(along_px, 0.0, seg_px);
                    float dist = length(vec2(perp, along_px - ac));
                    float shp = dot_shape(dist / radius, soft, blur);
                    if (shp <= 0.0) continue;
                    float fk = mod(j, dots);
                    float tw_ph = hash11(fi * 13.7 + fk * 0.917 + rib * 3.0);
                    float twinkle = pow(max(sin(g_t * (1.2 + 2.6 * tw_ph) * 3.0 + tw_ph * 61.0), 0.0), 36.0)
                        * P_SPARKLE * (1.0 - 0.85 * dl);
                    float inten = base_i * (1.0 + fold * 7.0 + twinkle * 5.0 + tip * 7.0) * shp;
                    float hl = clamp(fold * 0.55 + twinkle + tip, 0.0, 1.0);
                    dsum += vec4(tint_color(tint_k, hl) * inten, inten);
                }
                // Dots merging along the fibre: their line integral instead.
                float rho_p = abs(perp) / radius;
                float line = mix(0.991 * exp(-3.2 * rho_p * rho_p), 2.0 * sqrt(max(1.0 - rho_p * rho_p, 0.0)) * 0.95, blur)
                    * radius / sp_along + seg_px / sp_along * dot_shape(rho_p, soft, blur);
                float li_i = base_i * (1.0 + fold * 7.0 + tip * 7.0) * line;
                vec4 lsum = vec4(tint_color(tint_k, clamp(fold * 0.55 + tip, 0.0, 1.0)) * li_i, li_i);
                disc += mix(dsum, lsum, w_along);
            }
            float c = dens * cover;
            scene += mix(disc, vec4(col_c * c, c), w_perp);
        }
    }

    // Dust: one mote per cell of a jittered screen grid.
    if (P_PARTICLES > 0.0) {
        float count = 420.0 * P_PARTICLES;
        float cell = sqrt(res.x * res.y / count);
        vec2 base_cell = floor(frag / cell);
        // Motes sit in the middle 70% of their cell, so only the cell and
        // its three neighbours on the pixel's side can reach it.
        vec2 side = sign(fract(frag / cell) - 0.5);
        for (int oy = 0; oy <= 1; oy++) {
            for (int ox = 0; ox <= 1; ox++) {
                vec2 id = base_cell + vec2(float(ox), float(oy)) * side;
                vec3 seed = vec3(wl_hash12(id * 1.37 + 0.3), wl_hash12(id * 2.91 + 1.7), wl_hash12(id * 4.17 + 2.9));
                float z = ((seed.z - 0.5) * 6.0 - 0.6 + 0.18 * sin(g_t * 0.13 + seed.z * 40.0)) * mix(1.0, g_scale, 0.5);
                float depth = CAMERA - z;
                vec2 drift = 0.18 * vec2(sin(g_t * 0.21 + seed.x * 40.0), sin(g_t * 0.17 + seed.y * 40.0)) * g_K / depth;
                vec2 pos = (id + 0.15 + 0.7 * seed.xy) * cell + drift;
                float sz = 2.2 * P_THICKNESS * mix(0.5, 1.4, wl_hash12(id * 7.7));
                float base_r = max(0.5 * sz * g_dpr * CAMERA / depth, 0.45);
                float coc = min(P_DEPTH_OF_FIELD * abs(1.0 - focus / depth) * res.y * 0.035, 22.0 * g_dpr * 1.6);
                float radius = base_r + coc;
                float r_eval = max(radius, 1.0);
                float dist = length(frag - pos);
                if (dist >= r_eval) continue;
                float spill = max(10.0 * g_dpr, base_r);
                float energy = pow(base_r / min(radius, spill), 0.9) * pow(min(1.0, spill / radius), 2.0);
                energy *= (radius * radius) / (r_eval * r_eval);
                float twinkle = pow(max(sin(g_t * (0.8 + 1.6 * seed.y) * 2.0 + seed.z * 50.0), 0.0), 8.0);
                float inten = energy * (0.35 + 0.65 * twinkle) * mix(0.4, 1.0, seed.x) * 0.8 / P_THICKNESS;
                float shp = dot_shape(dist / r_eval, clamp(2.6 / r_eval, 0.14, 1.0), smoothstep(0.25, 0.7, coc / r_eval));
                float tint = P_GRADIENT == 0 ? 0.45 + 0.55 * seed.y : seed.y;
                vec3 col = tint_color(tint, 0.3 + 0.6 * twinkle * P_SPARKLE);
                scene += vec4(col * inten * shp, inten * shp);
            }
        }
    }

    vec2 uv = frag / res;
    float reach = max(P_VIGNETTE * 0.3, 1e-4);
    float mask = P_VIGNETTE > 0.0
        ? wl_smoother(uv.x / reach) * wl_smoother((1.0 - uv.x) / reach) * wl_smoother(uv.y / reach) * wl_smoother((1.0 - uv.y) / reach)
        : 1.0;
    vec2 gcell = floor(frag / g_dpr);
    float frame = mod(floor(clock * 30.0), 977.0);
    float grain = wl_hash12(gcell + frame * 17.31) + wl_hash12(gcell * 1.31 + frame * 3.7 + 9.1) - 1.0;

#if P_LIGHT_MODE
    float amount = scene.a * P_BRIGHTNESS;
    float ink = (1.0 - exp(-amount * 1.5)) * mask;
    ink = clamp(ink * (1.0 + grain * P_GRAIN * 2.0), 0.0, 1.0);
    vec3 hue = clamp(scene.rgb / max(scene.a, 1e-4), 0.0, 1.0);
    vec3 tone = hue * mix(1.0, 0.72, clamp(amount * 0.35, 0.0, 1.0));
    vec3 page = vec3(1.0) - P_BACKGROUND * 0.35;
    gl_FragColor = vec4(wl_over(page, vec4(tone * ink, ink)), 1.0);
#else
    vec3 light = (scene.rgb + bloom * P_GLOW * 0.7) * P_BRIGHTNESS;
    vec3 color = (1.0 - exp(-light * 1.25)) * mask;
    float peak = max(color.r, max(color.g, color.b));
    color = max(color + grain * P_GRAIN * (0.25 + 0.75 * sqrt(peak)), 0.0);
    gl_FragColor = vec4(wl_over(P_BACKGROUND, vec4(color, max(color.r, max(color.g, color.b)))), 1.0);
#endif
}
