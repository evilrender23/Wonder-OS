// GradientASCII — ported from React Bits <GradientASCII /> (see ../LICENSE-REACTBITS.md).
// Single pass: the half-res field target is evaluated per cell centre, the
// font atlas becomes hand-drawn stroke SDFs for the glyphs of three
// character sets, and the blurred quarter-res halo becomes a bilinear blend
// of the four nearest cells' average glyph ink (lit by the pixel's cell). Pointer scramble, click
// waves and the decoding intro are dropped.

vec3 g_lab0;
vec3 g_lab1;
vec3 g_lab2;
vec3 g_lab3;

// ---------- colour ----------
float cbrt(float x) {
    return sign(x) * pow(abs(x), 1.0 / 3.0);
}

vec3 to_lab(vec3 c) {
    vec3 l = wl_to_linear(c);
    float L = cbrt(0.4122214708 * l.r + 0.5363325363 * l.g + 0.0514459929 * l.b);
    float M = cbrt(0.2119034982 * l.r + 0.6806995451 * l.g + 0.1073969566 * l.b);
    float S = cbrt(0.0883024619 * l.r + 0.2817188376 * l.g + 0.6299787005 * l.b);
    return vec3(0.2104542553 * L + 0.793617785 * M - 0.0040720468 * S,
                1.9779984951 * L - 2.428592205 * M + 0.4505937099 * S,
                0.0259040371 * L + 0.7827717662 * M - 0.808675766 * S);
}

vec3 lab_to_rgb(vec3 lab) {
    float l = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    return vec3(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

vec3 ramp(float i) {
    if (i < 0.5) return g_lab0;
    if (i < 1.5) return g_lab1;
    if (i < 2.5) return g_lab2;
    return g_lab3;
}

vec4 tone_of(float t) {
    float last = float(P_COLORS - 1);
    float x = smoothstep(0.3, 1.0, t) * last;
    float index = floor(x);
    return vec4(mix(ramp(index), ramp(min(index + 1.0, last)), fract(x)), smoothstep(0.0, 0.38, t));
}

// ---------- field ----------
vec2 hash22s(vec2 p) {
    return wl_hash22(p) * 2.0 - 1.0;
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(hash22s(i), f);
    float b = dot(hash22s(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    float c = dot(hash22s(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    float d = dot(hash22s(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
    return 0.5 + 0.5 * mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec3 shape_horizon(vec2 p, float time, float aspect) {
    float span = 0.5 * max(aspect, 1.0);
    float x = p.x / span;
    float arch = -0.17 - 0.2 * x * x + 0.02 * sin(time * 0.6);
    float soft = mix(0.05, 0.22, P_SOFTNESS);
    float edge = p.y - arch;
    float rim = 0.38 * smoothstep(-soft, 0.42, edge);
    vec2 sun = vec2(0.08 * sin(time * 0.31) * span, 0.58);
    float spread = length((p - sun) / vec2(0.95 * span, 0.75));
    float sky = mix(0.55, 1.0, smoothstep(0.2, 0.95, spread));
    return vec3(rim, sky, smoothstep(0.18, 0.62, edge));
}

vec3 shape_pinch(vec2 p, float time, float aspect) {
    float span = 0.5 * max(aspect, 0.75);
    float x = p.x / span;
    float sway = 0.02 * sin(x * 1.7 + time * 0.55);
    float breathe = 1.0 + 0.06 * sin(time * 0.8);
    float width = (0.13 + 0.22 * x * x) * breathe * mix(0.85, 1.2, P_SOFTNESS);
    float y = abs(p.y - sway) / width;
    float t = exp(-pow(y, 2.2)) * (0.42 + 0.58 * exp(-x * x * 1.8));
    return vec3(t, t, 0.0);
}

vec3 shape_rise(vec2 p, float time, float aspect) {
    float wide = max(aspect, 0.9);
    vec2 center = vec2(0.05 * sin(time * 0.45) * wide, -0.7);
    vec2 radius = vec2(0.82 * wide, 0.64 * (1.0 + 0.04 * sin(time * 0.8)));
    float d = length((p - center) / radius);
    float soft = mix(0.6, 1.15, P_SOFTNESS);
    float t = clamp(pow(clamp(1.0 - d, 0.0, 1.0), soft) * 1.35, 0.0, 1.0);
    return vec3(t, t, 0.0);
}

vec3 shape_eclipse(vec2 p, float time, float aspect) {
    float fit = min(1.0, aspect * 1.25);
    float r = 0.23 * fit * (1.0 + 0.015 * sin(time * 0.9));
    float angle = atan(p.y, p.x);
    float rays = noise(vec2(cos(angle), sin(angle)) * 2.2 + time * 0.08) * 0.8 + 0.6;
    float d = length(p) - r;
    float reach = mix(0.07, 0.15, P_SOFTNESS) * rays * fit;
    float t = smoothstep(-0.006, 0.006 + 0.02 * P_SOFTNESS, d) * exp(-max(d, 0.0) / reach);
    return vec3(t, t, 0.0);
}

// Premultiplied sRGB colour + alpha of the light at uv (0..1, y up).
vec4 field(vec2 uv, float time, float aspect) {
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    vec2 q = p / P_SCALE;
    vec2 seed = vec2(P_VARIATION * 3.17, P_VARIATION * 1.91);
    vec2 warp = vec2(noise(q * 1.6 + seed + vec2(0.0, time * 0.3)),
                     noise(q * 1.6 + seed.yx + vec2(4.3, -time * 0.24))) - 0.5;
#if P_SHAPE == 3
    q += warp * P_FLOW * 0.04;
    vec3 shape = shape_eclipse(q, time, aspect);
#elif P_SHAPE == 2
    q += warp * P_FLOW * 0.18;
    vec3 shape = shape_rise(q, time, aspect);
#elif P_SHAPE == 1
    q += warp * P_FLOW * 0.18;
    vec3 shape = shape_pinch(q, time, aspect);
#else
    q += warp * P_FLOW * 0.18;
    vec3 shape = shape_horizon(q, time, aspect);
#endif
    vec2 t = clamp(shape.xy, 0.0, 1.0);
    vec4 mixed = mix(tone_of(t.x), tone_of(t.y), shape.z);
    vec3 color = wl_to_srgb(lab_to_rgb(mixed.rgb));
    return vec4(color * mixed.a, mixed.a);
}

// ---------- glyphs ----------
// Glyph space: units of the cell height, origin at the cell centre, y up.
float seg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

float ring(vec2 p, vec2 c, vec2 r) {
    vec2 q = p - c;
    float k0 = length(q / r);
    float k1 = length(q / (r * r));
    return abs(k0 * (k0 - 1.0) / max(k1, 1e-4));
}

// Elliptic arc from angle a0 counter-clockwise to a1 (radians, a1 > a0).
float arc(vec2 p, vec2 c, vec2 r, float a0, float a1) {
    vec2 q = p - c;
    float ang = atan(q.y / r.y, q.x / r.x);
    ang = a0 + mod(ang - a0, 6.2831853);
    if (ang <= a1) return ring(p, c, r);
    return min(length(q - r * vec2(cos(a0), sin(a0))), length(q - r * vec2(cos(a1), sin(a1))));
}

// Glyph ids: 0 ' ', 1 '=', 2 '+', 3 'x', 4 'X', 5 '0', 6 'S', 7 '8', 8 '#',
// 9 '@', 10 '.', 11 ':', 12 '-', 13 '*', 14 '%', 15 '1'.
float glyph_sd(float id, vec2 p, float hs) {
    p.y += 0.03;
    if (id < 0.5) return 1e3;
    if (id < 1.5) return min(seg(p, vec2(-0.17, 0.055), vec2(0.17, 0.055)),
                             seg(p, vec2(-0.17, -0.095), vec2(0.17, -0.095)));
    if (id < 2.5) return min(seg(p, vec2(-0.17, -0.02), vec2(0.17, -0.02)),
                             seg(p, vec2(0.0, -0.19), vec2(0.0, 0.15)));
    if (id < 3.5) return min(seg(p, vec2(-0.16, -0.275), vec2(0.16, 0.135)),
                             seg(p, vec2(0.16, -0.275), vec2(-0.16, 0.135)));
    if (id < 4.5) return min(seg(p, vec2(-0.18, -0.275), vec2(0.18, 0.275)),
                             seg(p, vec2(0.18, -0.275), vec2(-0.18, 0.275)));
    if (id < 5.5) return min(ring(p, vec2(0.0), vec2(0.165, 0.275)),
                             seg(p, vec2(-0.07, -0.11), vec2(0.07, 0.11)));
    if (id < 6.5) return min(arc(p, vec2(0.0, 0.135), vec2(0.155, 0.14), 0.35, 4.71),
                             arc(p, vec2(0.0, -0.135), vec2(0.17, 0.14), -2.8, 1.5708));
    if (id < 7.5) return min(ring(p, vec2(0.0, 0.145), vec2(0.14, 0.13)),
                             ring(p, vec2(0.0, -0.13), vec2(0.17, 0.145)));
    if (id < 8.5) return min(min(seg(p, vec2(-0.07, -0.26), vec2(-0.05, 0.22)),
                                 seg(p, vec2(0.07, -0.26), vec2(0.09, 0.22))),
                             min(seg(p, vec2(-0.19, 0.07), vec2(0.2, 0.07)),
                                 seg(p, vec2(-0.2, -0.11), vec2(0.19, -0.11))));
    if (id < 9.5) return min(min(arc(p, vec2(0.0, -0.02), vec2(0.195, 0.25), -0.25, 5.2),
                                 ring(p, vec2(-0.01, -0.03), vec2(0.075, 0.1))),
                             min(seg(p, vec2(0.075, 0.07), vec2(0.075, -0.1)),
                                 seg(p, vec2(0.075, -0.1), vec2(0.188, -0.07))));
    if (id < 10.5) return length(p - vec2(0.0, -0.245)) - 0.04 + hs;
    if (id < 11.5) return min(length(p - vec2(0.0, -0.245)), length(p - vec2(0.0, 0.07))) - 0.04 + hs;
    if (id < 12.5) return seg(p, vec2(-0.13, -0.02), vec2(0.13, -0.02));
    if (id < 13.5) {
        vec2 c = vec2(0.0, 0.11);
        return min(seg(p, c - vec2(0.0, 0.14), c + vec2(0.0, 0.14)),
                   min(seg(p, c - vec2(0.12, 0.07), c + vec2(0.12, 0.07)),
                       seg(p, c - vec2(0.12, -0.07), c + vec2(0.12, -0.07))));
    }
    if (id < 14.5) return min(seg(p, vec2(-0.17, -0.27), vec2(0.17, 0.27)),
                              min(ring(p, vec2(-0.1, 0.17), vec2(0.06, 0.075)),
                                  ring(p, vec2(0.1, -0.17), vec2(0.06, 0.075))));
    return min(seg(p, vec2(0.02, -0.275), vec2(0.02, 0.275)),
               min(seg(p, vec2(0.02, 0.275), vec2(-0.11, 0.18)),
                   seg(p, vec2(-0.13, -0.275), vec2(0.15, -0.275))));
}

// Average ink of each glyph (for the halo).
float glyph_ink(float id) {
    if (id < 0.5) return 0.0;
    if (id < 2.5) return 0.08;
    if (id < 3.5) return 0.09;
    if (id < 4.5) return 0.12;
    if (id < 5.5) return 0.16;
    if (id < 7.5) return 0.17;
    if (id < 9.5) return 0.21;
    if (id < 10.5) return 0.02;
    if (id < 12.5) return 0.04;
    if (id < 14.5) return 0.12;
    return 0.1;
}

float char_count() {
#if P_CHARACTERS == 2
    return 3.0;
#else
    return 10.0;
#endif
}

float glyph_id(float index) {
#if P_CHARACTERS == 1
    // " .:-=+*#%@"
    if (index < 0.5) return 0.0;
    if (index < 1.5) return 10.0;
    if (index < 2.5) return 11.0;
    if (index < 3.5) return 12.0;
    if (index < 4.5) return 1.0;
    if (index < 5.5) return 2.0;
    if (index < 6.5) return 13.0;
    if (index < 7.5) return 8.0;
    if (index < 8.5) return 14.0;
    return 9.0;
#elif P_CHARACTERS == 2
    // " 01"
    if (index < 0.5) return 0.0;
    if (index < 1.5) return 5.0;
    return 15.0;
#else
    // " =+xX0S8#@"
    return index;
#endif
}

vec3 paper() {
    vec3 bg = P_BACKGROUND;
    return dot(bg, vec3(0.2126, 0.7152, 0.0722)) > 0.5 ? bg : vec3(0.965, 0.969, 0.976);
}

// Per cell: glyph id, its alpha factor, and the field colour behind it.
struct Cell {
    float id;
    float alpha;
    float focus;
    vec4 field;
};

Cell cell_at(vec2 cell, vec4 f, float glyphTime) {
    Cell c;
    c.field = f;
#if P_LIGHT_MODE
    vec3 visible = f.rgb + (1.0 - f.a);
    float value = clamp((1.0 - dot(visible, vec3(0.2126, 0.7152, 0.0722))) * 1.5, 0.0, 1.0);
#else
    float value = dot(f.rgb, vec3(0.2126, 0.7152, 0.0722));
#endif
    float count = char_count();
    float pick = wl_hash12(cell + P_VARIATION * 7.31);
    float level = wl_hash12(cell * 1.73 + 4.1 + P_VARIATION);
    float tick = floor(glyphTime * 9.0 + level * 9.0);
    float wobble = (wl_hash12(cell * 2.31 + tick * 17.7) - 0.5) * P_FLICKER * 0.4 + (level - 0.5) * 0.1;
    float tone = clamp(pow(value, 0.75) + wobble, 0.0, 1.0);
    float index = floor(tone * (count - 1.0) + 0.5);
    float present = step(pick, P_DENSITY * mix(1.0, 0.3 + tone * 1.1, 0.55));
    float cutoff = smoothstep(0.03, 0.1, value);
    c.focus = step(wl_hash12(cell * 0.71 + 9.2 + P_VARIATION), P_DEPTH_OF_FIELD);
    c.id = glyph_id(index);
    c.alpha = P_GLYPH_OPACITY * present * cutoff * mix(1.0, 0.35 + 0.65 * level, 0.55) * (1.0 - c.focus * 0.5);
    return c;
}

vec4 glyph_color(Cell c, float coverage) {
    float alpha = c.alpha * coverage;
#if P_LIGHT_MODE
    vec3 ink = (c.field.rgb + (1.0 - c.field.a)) * 0.8;
    return vec4(ink * alpha, alpha);
#else
    vec3 light = c.field.rgb * 0.55 + vec3(0.03) * c.field.a;
    return vec4(light * alpha, 0.6 * c.field.a * alpha);
#endif
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float clock = wl_time();
    float time = clock * 0.5;
    float glyphTime = clock;
    float aspect = res.x / res.y;

    g_lab0 = to_lab(P_COLOR_1);
    g_lab1 = to_lab(P_COLOR_2);
    g_lab2 = to_lab(P_COLOR_3);
    g_lab3 = to_lab(P_COLOR_4);


    vec2 cellPx = vec2(P_CELL_SIZE * 0.6, P_CELL_SIZE);
    vec2 offset = (res - ceil(res / cellPx) * cellPx) * 0.5;
    vec2 coord = (frag - offset) / cellPx;

    // The four cells around the pixel: one draws the glyph, all four feed
    // the halo (bilinear, like the blurred quarter-res glyph layer).
    vec2 cc = coord - 0.5;
    vec2 c0 = floor(cc);
    vec2 fr = fract(cc);
    vec2 own = floor(coord);
    vec4 glyph = vec4(0.0);
    vec4 halo = vec4(0.0);
    // One field sample per pixel cell: the neighbours reuse it for picking
    // their glyph (only their faint, blurred halo depends on it).
    vec4 cellField = field(clamp(((own + 0.5) * cellPx + offset) / res, 0.0, 1.0), time, aspect);
    for (int k = 0; k < 4; k++) {
        vec2 o = vec2(mod(float(k), 2.0), floor(float(k) / 2.0));
        vec2 cell = c0 + o;
        Cell c = cell_at(cell, cellField, glyphTime);
        vec2 wt = mix(1.0 - fr, fr, o);
        halo += glyph_color(c, glyph_ink(c.id) * 1.6) * wt.x * wt.y;
        if (cell.x == own.x && cell.y == own.y && c.alpha > 0.0) {
            vec2 local = coord - cell;
            vec2 g = (local - 0.5) * vec2(0.6, 1.0);
            float hs = max(0.024, 0.42 / P_CELL_SIZE);
            float sd = glyph_sd(c.id, g, hs);
            float aa = (0.8 + c.focus * 1.6) / P_CELL_SIZE;
            float coverage = clamp((hs - sd) / aa + 0.5, 0.0, 1.0);
            coverage *= min(1.0, P_CELL_SIZE * 0.042 + 0.25);
            glyph = glyph_color(c, coverage);
        }
    }
    halo *= P_GLOW;
    vec4 base = field(frag / res, time, aspect) * P_GRADIENT_OPACITY;

    vec4 color;
#if P_LIGHT_MODE
    color = glyph + base * (1.0 - glyph.a);
    color += halo * 0.6 * (1.0 - color.a);
#else
    color = base + glyph + halo;
#endif
    float frame = mod(floor(clock * 24.0), 97.0);
    float grain = (wl_hash12(frag + frame * vec2(17.0, 59.0)) + wl_hash12(frag * 1.31 + frame * 7.0) - 1.0) * P_GRAIN;
    float body = clamp(color.a, 0.0, 1.0);
    color.rgb += grain * (0.25 + body) * 0.5;
    color.a = clamp(color.a, 0.0, 1.0);
    color.rgb = clamp(color.rgb, 0.0, color.a);

#if P_LIGHT_MODE
    gl_FragColor = vec4(wl_over(paper(), color), 1.0);
#else
    gl_FragColor = vec4(wl_over(P_BACKGROUND, color), 1.0);
#endif
}
