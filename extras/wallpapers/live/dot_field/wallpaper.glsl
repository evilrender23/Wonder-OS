// DotField — ported from React Bits <DotField /> (see ../LICENSE-REACTBITS.md).
// The instanced dots become a per-pixel search: undo the lens and the wave
// to find which layout cell the pixel came from, then evaluate the few
// neighbouring dots analytically (grid/hex/scatter: 3x3 cells; sunflower
// spiral: the 2x2 Fibonacci-lattice candidates). The cursor lens can't follow
// a pointer here; the optional «Lente errante» lets it wander on its own.

const float PI = 3.14159265;
const float PHI = 1.61803399;
// 1 - 1/phi (golden angle in turns), and fract(256 * it) for the split product.
const float GOLD = 0.38196601;
const float GOLD256 = 0.78329888;

vec3 oklab_to_rgb(vec3 lab) {
    float l = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s = lab.x - 0.0894841775 * lab.y - 1.291485548 * lab.z;
    vec3 lms = vec3(l * l * l, m * m * m, s * s * s);
    vec3 lin = clamp(vec3(
        4.0767416621 * lms.x - 3.3077115913 * lms.y + 0.2309699292 * lms.z,
        -1.2684380046 * lms.x + 2.6097574011 * lms.y - 0.3413193965 * lms.z,
        -0.0041960863 * lms.x - 0.7034186147 * lms.y + 1.707614701 * lms.z
    ), 0.0, 1.0);
    return wl_to_srgb(lin);
}

vec3 rgb_to_oklab(vec3 srgb) {
    vec3 c = wl_to_linear(srgb);
    vec3 lms = vec3(
        0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b,
        0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b,
        0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b
    );
    lms = pow(max(lms, vec3(0.0)), vec3(1.0 / 3.0));
    return vec3(
        0.2104542553 * lms.x + 0.793617785 * lms.y - 0.0040720468 * lms.z,
        1.9779984951 * lms.x - 2.428592205 * lms.y + 0.4505937099 * lms.z,
        0.0259040371 * lms.x + 0.7827717662 * lms.y - 0.808675766 * lms.z
    );
}

// Light mode darkens bright inks so they read on a light page.
vec3 to_ink(vec3 srgb) {
    vec3 lab = rgb_to_oklab(srgb);
#if P_LIGHT_MODE
    float hue = degrees(atan(lab.z, lab.y));
    float warmth = clamp(1.0 - abs(hue - 85.0) / 40.0, 0.0, 1.0);
    float ceiling = 0.6 + 0.16 * warmth * warmth * (3.0 - 2.0 * warmth);
    if (lab.x > ceiling) {
        float boost = 1.0 + (lab.x - ceiling) * 1.6;
        lab = vec3(ceiling, lab.yz * boost);
    }
#endif
    return lab;
}

vec3 blend_lab(vec3 a, vec3 b, float t) {
    vec2 ab = mix(a.yz, b.yz, t);
    float chroma = mix(length(a.yz), length(b.yz), t);
    float len = length(ab);
    return vec3(mix(a.x, b.x, t), len > 1e-5 ? ab * (chroma / len) : ab);
}

vec3 wave(vec2 p, float t) {
    float ay = p.x * 0.018 + t + 1.1 * sin(p.y * 0.016 - t * 0.4);
    float ax = p.y * 0.021 + t * 0.7 + 1.3 * sin(p.x * 0.013 + t * 0.3);
    float spread = cos(ay) * 0.0176 * cos(p.y * 0.016 - t * 0.4) + 0.5 * cos(ax) * 0.0169 * cos(p.x * 0.013 + t * 0.3);
    return P_WAVE_AMPLITUDE * vec3(0.5 * sin(ax), sin(ay), spread);
}

float step_px() { return P_DOT_RADIUS * 2.0 + P_DOT_SPACING; }

float fib(float k) { return floor(pow(PHI, k) / 2.2360680 + 0.5); }

// Distance (in turns) from F_k * GOLD to the nearest integer, exactly.
float fib_turn(float k) {
    float e = pow(PHI, -(k - 2.0)) * (1.0 - pow(PHI, -4.0)) / 2.2360680;
    return mod(k, 2.0) < 0.5 ? e : -e;
}

vec2 spiral_pos(float j) {
    float sp = step_px() / sqrt(PI);
    float hi = floor(j / 256.0);
    float lo = j - hi * 256.0;
    float turns = fract(hi * GOLD256 + lo * GOLD);
    float a = turns * 2.0 * PI;
    return sp * sqrt(j + 0.5) * vec2(cos(a), sin(a));
}

// Dot n of the neighbourhood around `est` (relative to the centre, y down):
// returns the layout position, id.xy = hash inputs.
vec2 candidate(vec2 est, int n, out vec2 id) {
    float st = step_px();
#if P_PATTERN == 2
    float sp = st / sqrt(PI);
    float i = max(dot(est, est) / (sp * sp) - 0.5, 0.0);
    float th = atan(est.y, est.x) / (2.0 * PI);
    float k = max(2.0, floor(log(max(i, 1.0) * 4.0 * PI) / (2.0 * log(PHI))) + 1.0);
    float fa = fib(k);
    float fb = fib(k + 1.0);
    float aa = fib_turn(k);
    float ab = fib_turn(k + 1.0);
    float det = aa * fb - ab * fa;
    float a = floor((th * fb - ab * i) / det);
    float b = floor((aa * i - th * fa) / det);
    float u = mod(float(n), 2.0);
    float v = floor(float(n) / 2.0);
    float j = (a + u) * fa + (b + v) * fb;
    id = vec2(j, 0.0);
    return j < 0.0 ? vec2(1e5) : spiral_pos(j);
#else
    float dx = mod(float(n), 3.0) - 1.0;
    float dy = floor(float(n) / 3.0) - 1.0;
    float row_step = P_PATTERN == 1 ? st * 0.8660254 : st;
    float row = floor(est.y / row_step + 0.5) + dy;
    float shift = (P_PATTERN == 1 && mod(row, 2.0) > 0.5) ? st * 0.5 : 0.0;
    float col = floor((est.x - shift) / st + 0.5) + dx;
    vec2 p = vec2(col * st + shift, row * row_step);
    if (P_PATTERN == 3) {
        p.x += (wl_hash12(vec2(col, row) + vec2(17.31, 3.7)) - 0.5) * st * 0.8;
        p.y += (wl_hash12(vec2(col, row) + vec2(-41.2, 29.9)) - 0.5) * st * 0.8;
    }
    id = vec2(col, row);
    return p;
#endif
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    // The component lays dots out in CSS pixels with a top-left origin.
    vec2 q = vec2(frag.x, res.y - frag.y);
    vec2 center = res * 0.5;
    // time += dt * speed (default 1).
    float t = wl_time() + 37.0;

    // Lens: strength/twist as the component sets them with no hold-to-zoom.
    float lens_on = 0.0;
    vec2 lens = center;
#if P_WANDER_LENS
    lens_on = 1.0;
    float lt = wl_time();
    lens = center + vec2(0.34 * res.x * sin(lt * 0.13), 0.3 * res.y * sin(lt * 0.19 + 1.3));
#endif
    float lens_r = P_CURSOR_RADIUS;
    float k = clamp(P_BULGE_STRENGTH >= 0.0 ? P_BULGE_STRENGTH * 1.2 : P_BULGE_STRENGTH * 0.7, -0.85, 1.45) * lens_on;
    float twist = radians(P_TWIST) * lens_on;

    // Undo the lens: solve r * (1 + k w(r)) = |q - lens| (monotone in r).
    vec2 src = q;
    if (lens_on > 0.0) {
        vec2 rel = q - lens;
        float rd = length(rel);
        float r = rd;
        for (int it = 0; it < 6; it++) {
            float x = r / lens_r;
            float y = clamp(1.0 - x * x, 0.0, 1.0);
            float w = y * y * y;
            float g = r * (1.0 + k * w) - rd;
            float dg = 1.0 + k * (w - 6.0 * x * x * y * y);
            r = max(r - g / max(dg, 0.1), 0.0);
        }
        float x = r / lens_r;
        float y = clamp(1.0 - x * x, 0.0, 1.0);
        float th = -twist * y * y * y;
        vec2 dir = rd > 1e-4 ? rel / rd : vec2(0.0);
        dir = vec2(cos(th) * dir.x - sin(th) * dir.y, sin(th) * dir.x + cos(th) * dir.y);
        src = lens + dir * r;
    }
    // Undo the wave by fixed-point iteration.
    vec2 est = src;
    if (P_WAVE_AMPLITUDE > 0.0) {
        for (int it = 0; it < 3; it++) est = src - wave(est, t).xy;
    }

    // Colours (per pixel: the gradient barely moves across one dot).
    float ang = radians(P_GRADIENT_ANGLE);
    vec2 gdir = vec2(sin(ang), -cos(ang));
    float extent = max(abs(res.x * gdir.x) + abs(res.y * gdir.y), 1.0);
    float gt = clamp(dot(est - center, gdir) / extent + 0.5, 0.0, 1.0);
    vec3 tone = blend_lab(to_ink(P_GRADIENT_FROM), to_ink(P_GRADIENT_TO), gt);
    vec3 deep = vec3(tone.x * 0.72, tone.yz * 1.3);
    float glow_weight = 1.0;
#if P_LIGHT_MODE
    vec3 glow_lab = rgb_to_oklab(P_GLOW_COLOR);
    glow_weight = clamp((length(glow_lab.yz) - 0.02) / 0.06, 0.0, 1.0);
    float base_opacity = min(1.0, P_BASE_OPACITY * 1.3);
    float halo_on = 0.0;
#else
    float base_opacity = P_BASE_OPACITY;
    float halo_on = 1.0;
#endif
    vec3 base_rgb = oklab_to_rgb(tone);
    vec3 lit_rgb = oklab_to_rgb(mix(deep, to_ink(P_GLOW_COLOR), glow_weight));

    vec4 acc = vec4(0.0);
    vec2 est_c = est - center;
    for (int n = 0; n < 9; n++) {
#if P_PATTERN == 2
        if (n >= 4) break;
#endif
        vec2 id;
        vec2 dot_rel = candidate(est_c, n, id);
        vec2 dot_pos = center + dot_rel;
        float hz = wl_hash12(id + vec2(3.1, 7.7));
        float hw = wl_hash12(id + vec2(-11.9, 4.3));

        vec3 swell = vec3(0.0);
        if (P_WAVE_AMPLITUDE > 0.0) swell = wave(dot_pos, t);
        vec2 s = dot_pos + swell.xy;

        vec2 disp = s;
        float scale = 1.0;
        if (lens_on > 0.0) {
            vec2 rel = s - lens;
            float x = length(rel) / lens_r;
            float y = clamp(1.0 - x * x, 0.0, 1.0);
            float w = y * y * y;
            float grow = 1.0 + k * w;
            float spread = 1.0 + k * (w - 6.0 * x * x * y * y);
            float th = twist * w;
            vec2 tr = vec2(cos(th), sin(th));
            disp = lens + vec2(tr.x * rel.x - tr.y * rel.y, tr.y * rel.x + tr.x * rel.y) * grow;
            scale = sqrt(max(grow * spread, 0.02));
        }

        float spark = 0.0;
#if P_SPARKLE
        float phase = 0.5 + 0.5 * sin(t * (0.55 + hw * 0.9) + hz * 6.2831853);
        spark = pow(phase, 48.0) * step(0.72, fract(hz * 7.31 + hw * 3.17));
#endif
        float pool = 0.0;
        if (lens_on > 0.0 && P_GLOW_RADIUS > 0.0) {
            float g = length(disp - lens) / P_GLOW_RADIUS;
            float qq = clamp(1.0 - g * g, 0.0, 1.0);
            pool = qq * qq * P_GLOW_INTENSITY;
        }

        float radius = P_DOT_RADIUS * (1.0 + 0.7 * spark);
        float device = radius * scale;
        float fade = 1.0;
        if (device < 0.75) {
            fade = device * device / 0.5625;
            radius = 0.75 / scale;
        }
        float halo = pool * halo_on;
        float halo_width = 2.0 + radius * 1.5;
        float pad = radius + 1.5 / scale + (halo > 0.0 ? halo_width * 2.5 : 0.0);

        vec3 color = mix(base_rgb, lit_rgb, clamp(pool, 0.0, 1.0) * 0.8);
#if P_LIGHT_MODE
        color = mix(color, color * 0.6, spark * 0.55);
#else
        color = mix(color, vec3(1.0), spark * 0.55);
#endif
        float shade = clamp(1.0 - swell.z * 2.5, 0.4, 1.6);
        float valpha = mix(min(base_opacity * shade, 1.0), 1.0, clamp(max(pool, spark), 0.0, 1.0)) * fade;

        vec2 local = (q - disp) / scale;
        float dist = length(local);
        if (dist > pad) continue;
        float d = dist - radius;
        float edge = 1.2 / scale;
        float cover = 1.0 - smoothstep(-0.5 * edge, 0.5 * edge, d);
        // The quad clipped the halo at `pad`; ease it out instead of a seam.
        float glow = halo * exp(-max(d, 0.0) / halo_width) * 0.5 * smoothstep(pad, pad * 0.6, dist);
        float alpha = min(cover + glow, 1.0) * valpha;
        acc = vec4(color * alpha, alpha) + acc * (1.0 - alpha);
    }

#if P_LIGHT_MODE
    vec3 page = P_LIGHT_BACKGROUND;
#else
    vec3 page = P_BACKGROUND;
#endif
    gl_FragColor = vec4(wl_over(page, acc), 1.0);
}
