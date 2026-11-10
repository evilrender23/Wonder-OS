// FaultyTerminal — ported from React Bits <FaultyTerminal /> (see ../LICENSE-REACTBITS.md).
// Single pass: the per-cell pattern pass (and its phosphor-persistence
// feedback) is evaluated inline for the 4 cells nearest the pixel; the
// afterglow becomes a soft switching threshold. The light resting in the
// middle (the cursor's idle position) is kept; pointer, clicks and the
// switch-on intro are dropped.

float g_time;     // pattern clock (iTime / 3)
float g_itime;    // component iTime
vec2 g_grid;
vec2 g_base;
mat2 g_rot_fbm;
mat2 g_rot01;
float g_wobble;
vec2 g_side;      // which neighbours (x, y) sit nearest the pixel
float c_base, c_x, c_y, c_xy;

mat2 rotate(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, -s, s, c);
}

float noise(vec2 p) {
    return sin(p.x * 10.0) * sin(p.y * g_wobble) + 0.2;
}

float fbm(vec2 p) {
    p *= 1.1;
    float f = 0.0;
    float amp = 0.5 * P_NOISE_AMP;
    f += amp * noise(p);
    p = g_rot_fbm * p * 2.0;
    amp *= 0.454545;
    f += amp * noise(p);
    p = g_rot_fbm * p * 2.0;
    amp *= 0.454545;
    f += amp * noise(p);
    return f;
}

float pattern(vec2 p) {
    mat2 rot1 = mat2(0.995004, -0.0998334, 0.0998334, 0.995004);
    vec2 q = vec2(fbm(p + vec2(1.0)), fbm(g_rot01 * p + vec2(1.0)));
    vec2 r = vec2(fbm(rot1 * q), fbm(q));
    return fbm(p + r);
}

// The cell pass: how strongly cell `cell` is lit.
float cell_intensity(vec2 cell) {
    vec2 s = cell / g_grid;
    float intensity = pattern(s * 0.1) * 1.3 - 0.03;
    // The cursor's resting light in the middle of the screen.
    float dist = distance(s, vec2(0.5) * P_SCALE);
    float influence = exp(-dist * 8.0) * P_CENTER_LIGHT * 10.0;
    intensity += influence + sin(dist * 20.0 - g_itime * 5.0) * 0.1 * influence;
    return intensity;
}

float cell_value(vec2 cell) {
    vec2 o = abs(cell - g_base);
    if (o.y > 0.5) return o.x > 0.5 ? c_xy : c_y;
    return o.x > 0.5 ? c_x : c_base;
}

// Ring n of a block lights up when the intensity clears 0.1 + n / 16; the
// soft edge stands in for the phosphor afterglow (persistence).
float ring(float n, float intensity) {
    float th = 0.1 + n * 0.0625;
    float w = 0.002 + 0.06 * P_PERSISTENCE;
    return smoothstep(th - w, th, intensity);
}

float level_of(float intensity) {
    return clamp((intensity - 0.1) / 0.6, 0.0, 1.0);
}

float hash_cell(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

vec2 font(float g) {
    if (g < 0.5) return vec2(22318.0, 467.0);
    if (g < 1.5) return vec2(4292.0, 452.0);
    if (g < 2.5) return vec2(14863.0, 993.0);
    if (g < 3.5) return vec2(14863.0, 496.0);
    if (g < 4.5) return vec2(32041.0, 264.0);
    if (g < 5.5) return vec2(15423.0, 496.0);
    if (g < 6.5) return vec2(15406.0, 465.0);
    if (g < 7.5) return vec2(8735.0, 132.0);
    if (g < 8.5) return vec2(14894.0, 465.0);
    if (g < 9.5) return vec2(31278.0, 464.0);
    if (g < 10.5) return vec2(32302.0, 561.0);
    if (g < 11.5) return vec2(15919.0, 497.0);
    if (g < 12.5) return vec2(1086.0, 961.0);
    if (g < 13.5) return vec2(17967.0, 497.0);
    if (g < 14.5) return vec2(15423.0, 993.0);
    if (g < 15.5) return vec2(15423.0, 33.0);
    if (g < 16.5) return vec2(14464.0, 4.0);
    if (g < 17.5) return vec2(14336.0, 0.0);
    if (g < 18.5) return vec2(8322.0, 68.0);
    if (g < 19.5) return vec2(4368.0, 34.0);
    if (g < 20.5) return vec2(15008.0, 21.0);
    if (g < 21.5) return vec2(992.0, 31.0);
    return vec2(0.0, 992.0);
}
float font_bit(float g, float bit) {
    vec2 rows = font(g);
    float word = bit < 15.0 ? rows.x : rows.y;
    float b = bit < 15.0 ? bit : bit - 15.0;
    return mod(floor(word / exp2(b)), 2.0);
}

float digit(vec2 p) {
    vec2 cell = floor(p * g_grid);
    float intensity = cell_value(cell);
    p = fract(p * g_grid);
    p *= P_DIGIT_SIZE;

    float px5 = p.x * 5.0;
    float py5 = (1.0 - p.y) * 5.0;
    float x = fract(px5);
    float y = fract(py5);
    float i = floor(py5) - 2.0;
    float j = floor(px5) - 2.0;
    float inside = step(0.0, p.x) * step(p.x, 1.0) * step(0.0, p.y) * step(p.y, 1.0);

#if P_GLYPHS == 2
    float level = level_of(intensity);
    float phase = hash_cell(cell * 1.37 + 4.1);
    float epoch = floor(g_itime * 1.2 + phase * 7.0);
    float pick = hash_cell(cell + vec2(epoch * 0.731, epoch * 1.913));
    float glyph = min(floor(pick * 23.0), 22.0);
    float bit = clamp((i + 2.0) * 5.0 + (j + 2.0), 0.0, 24.0);
    float lit = font_bit(glyph, bit);
    float glow = max(ring(0.0, intensity), level);
    float shade = 0.35 + 0.65 * level;
    return inside * lit * glow * shade * (0.2 + y * 0.8) * (0.75 + x * 0.25);
#else
    float on = ring(i * i + j * j, intensity);
#if P_GLYPHS == 1
    vec2 d = vec2(x, y) - 0.5;
    float radius = 0.36 + 0.06 * on;
    float edge = fwidth(px5) * 0.75;
    float dot_mask = 1.0 - smoothstep(radius - edge, radius + edge, length(d));
    return inside * on * dot_mask * (0.55 + 0.45 * (1.0 - length(d) / 0.5));
#else
    return inside * on * (0.2 + y * 0.8) * (0.75 + x * 0.25);
#endif
#endif
}

float on_off(float a, float b, float c) {
    return step(c, sin(g_itime + a * cos(g_itime * b))) * P_FLICKER;
}

float displace(vec2 look) {
    float y = look.y - mod(g_itime * 0.25, 1.0);
    float window = 1.0 / (1.0 + 50.0 * y * y);
    return sin(look.y * 20.0 + g_itime) * 0.0125 * on_off(4.0, 2.0, 0.8) * (1.0 + cos(g_itime * 60.0)) * window;
}

vec2 get_light(vec2 p) {
    float bar = step(mod(p.y + g_time * 20.0, 1.0), 0.2) * 0.4 + 1.0;
    bar *= P_SCANLINE;
    p.x += displace(p) * P_GLITCH;
    float middle = digit(p);
    const float off = 0.002;
    float sum = digit(p + vec2(-off, -off)) + digit(p + vec2(0.0, -off)) + digit(p + vec2(off, -off)) +
                digit(p + vec2(-off, 0.0)) + middle + digit(p + vec2(off, 0.0)) +
                digit(p + vec2(-off, off)) + digit(p + vec2(0.0, off)) + digit(p + vec2(off, off));
    return vec2(0.9 * middle, sum * 0.1 * bar);
}

float emission(float intensity) {
#if P_GLYPHS == 2
    float level = level_of(intensity);
    return max(ring(0.0, intensity), level) * (0.35 + 0.65 * level) * 0.45;
#else
    return (ring(0.0, intensity) + 4.0 * (ring(1.0, intensity) + ring(2.0, intensity) + ring(4.0, intensity) + ring(8.0, intensity))
        + 8.0 * ring(5.0, intensity)) / 25.0;
#endif
}

float g_sigma2;
float g_center;
vec2 g_px;

float glow_term(vec2 p, float value, vec2 offset) {
    vec2 d = (p - (g_base + offset + g_center) / g_grid) * g_px;
    return emission(value) * exp(-dot(d, d) / g_sigma2);
}

// Phosphor bloom from the four glyphs nearest the pixel (the component
// sums 3x3 cells; the far ones add under 2%).
float glow_at(vec2 p, vec2 res) {
    vec2 cell_px = res / (P_SCALE * g_grid);
    float sigma = 0.5 * min(cell_px.x, cell_px.y);
    g_sigma2 = 2.0 * sigma * sigma;
    g_px = res / P_SCALE;
    return glow_term(p, c_base, vec2(0.0)) + glow_term(p, c_x, vec2(g_side.x, 0.0))
         + glow_term(p, c_y, vec2(0.0, g_side.y)) + glow_term(p, c_xy, g_side);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    // iTime advances at timeScale (default 0.3); variation offsets it.
    g_itime = wl_time() * 0.3 + P_VARIATION * 3.0;
    g_time = g_itime * 0.333333;
    g_rot_fbm = rotate(g_time * 0.02);
    g_rot01 = rotate(0.1 * g_time);
    g_wobble = 3.0 + sin(g_time * 0.090909);
    float columns = min(1.0, res.x / res.y / 1.5);
    g_grid = vec2(P_GRID_X * columns, P_GRID_Y) * 15.0;

    vec2 uv = frag / res;
    if (P_CURVATURE != 0.0) {
        vec2 c = uv * 2.0 - 1.0;
        c *= 1.0 + P_CURVATURE * dot(c, c);
        uv = c * 0.5 + 0.5;
    }
    vec2 p = uv * P_SCALE;

    // Evaluate the cell pass once for the 3x3 cells around the (glitched) pixel.
    vec2 shifted = p + vec2(displace(p) * P_GLITCH, 0.0);
    g_base = floor(shifted * g_grid);
    g_center = 0.5 * min(1.0, 1.0 / max(P_DIGIT_SIZE, 0.001));
    g_side = vec2(fract(shifted * g_grid).x < g_center ? -1.0 : 1.0,
                  fract(shifted * g_grid).y < g_center ? -1.0 : 1.0);
    c_base = cell_intensity(g_base);
    c_x = cell_intensity(g_base + vec2(g_side.x, 0.0));
    c_y = cell_intensity(g_base + vec2(0.0, g_side.y));
    c_xy = cell_intensity(g_base + g_side);

    vec2 light_g = get_light(p);
    vec2 light_r = light_g;
    vec2 light_b = light_g;
    if (P_CHROMATIC > 0.0) {
        vec2 ca = vec2(P_CHROMATIC) / res;
        light_r = get_light(p + ca);
        light_b = get_light(p - ca);
    }
    if (P_GLOW > 0.0) {
        float bloom = P_GLOW * 0.2 * glow_at(shifted, res);
        light_r.y += bloom;
        light_g.y += bloom;
        light_b.y += bloom;
    }

    vec3 core = vec3(light_r.x, light_g.x, light_b.x);
    vec3 halo = vec3(light_r.y, light_g.y, light_b.y);
    vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    vec3 tint = P_TINT;
    float peak = max(tint.r, max(tint.g, tint.b));
    float phosphor = clamp(P_GLOW, 0.0, 1.0);
    vec3 vivid_tint = clamp(mix(vec3(dot(tint, luma)), tint, 1.0 + 0.8 * phosphor), 0.0, 1.0);
    vec3 core_tint = mix(tint, vec3(peak), phosphor * 0.45 * smoothstep(0.45, 0.9, core));
    vec3 col = core * core_tint + halo * vivid_tint;
    col *= P_BRIGHTNESS;

    if (P_VIGNETTE > 0.0) {
        float edge = 0.04 + 0.22 * P_VIGNETTE;
        vec2 fade = smoothstep(vec2(0.0), vec2(edge), uv) * smoothstep(vec2(0.0), vec2(edge), 1.0 - uv);
        float mask = fade.x * fade.y;
        mask = mask * mask * (3.0 - 2.0 * mask);
        float keep = mix(1.0, mask, clamp(P_VIGNETTE * 1.5, 0.0, 1.0));
        col *= keep;
        core *= keep;
        halo *= keep;
    }

#if P_LIGHT_MODE
    float lum = dot(tint, luma);
    vec3 hue = tint / max(peak, 0.0001);
    vec3 vivid = clamp(mix(vec3(dot(hue, luma)), hue, 1.6), 0.0, 1.0);
    vivid = clamp(vivid * (0.3 / max(dot(vivid, luma), 0.0001)), 0.0, 1.0);
    vec3 ink = mix(tint, vivid, smoothstep(0.3, 0.6, lum));
    vec3 cover = clamp((core * 1.15 + halo * 0.4) * P_BRIGHTNESS, 0.0, 0.94);
    float alpha = max(cover.r, max(cover.g, cover.b));
    vec3 page = vec3(1.0) - P_BACKGROUND * 0.35;
    gl_FragColor = vec4(wl_over(page, vec4(alpha - cover * (1.0 - ink), alpha)), 1.0);
#else
    col = clamp(col, 0.0, 1.0);
    gl_FragColor = vec4(wl_over(P_BACKGROUND, vec4(col, max(col.r, max(col.g, col.b)))), 1.0);
#endif
}
