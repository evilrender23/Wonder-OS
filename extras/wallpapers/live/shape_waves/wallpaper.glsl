// ShapeWaves — ported from React Bits <ShapeWaves /> (see ../LICENSE-REACTBITS.md).
// The WebGPU scene pass ports directly. The bloom (half-res separable
// gaussian of the bright glyphs) becomes a sum over the neighbouring cells,
// each glyph treated as a gaussian blob of its glow energy. Cursor splashes
// (a CPU wave simulation), the intro wave and the text cutout are dropped.

const vec2 SEED = vec2(12.9898, 78.233);
const float GLOW_THRESHOLD = 0.6;

vec3 mod289_3(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289_4(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289_4(((x * 34.0) + 10.0) * x); }
vec4 taylor_inv_sqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }
vec3 fade_curve(vec3 t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float cnoise(vec3 P) {
    vec3 Pi0 = floor(P);
    vec3 Pi1 = Pi0 + vec3(1.0);
    Pi0 = mod289_3(Pi0);
    Pi1 = mod289_3(Pi1);
    vec3 Pf0 = fract(P);
    vec3 Pf1 = Pf0 - vec3(1.0);
    vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
    vec4 iy = vec4(Pi0.yy, Pi1.yy);
    vec4 iz0 = Pi0.zzzz;
    vec4 iz1 = Pi1.zzzz;

    vec4 ixy = permute(permute(ix) + iy);
    vec4 ixy0 = permute(ixy + iz0);
    vec4 ixy1 = permute(ixy + iz1);

    vec4 gx0 = ixy0 * (1.0 / 7.0);
    vec4 gy0 = fract(floor(gx0) * (1.0 / 7.0)) - 0.5;
    gx0 = fract(gx0);
    vec4 gz0 = vec4(0.5) - abs(gx0) - abs(gy0);
    vec4 sz0 = step(gz0, vec4(0.0));
    gx0 -= sz0 * (step(vec4(0.0), gx0) - 0.5);
    gy0 -= sz0 * (step(vec4(0.0), gy0) - 0.5);

    vec4 gx1 = ixy1 * (1.0 / 7.0);
    vec4 gy1 = fract(floor(gx1) * (1.0 / 7.0)) - 0.5;
    gx1 = fract(gx1);
    vec4 gz1 = vec4(0.5) - abs(gx1) - abs(gy1);
    vec4 sz1 = step(gz1, vec4(0.0));
    gx1 -= sz1 * (step(vec4(0.0), gx1) - 0.5);
    gy1 -= sz1 * (step(vec4(0.0), gy1) - 0.5);

    vec3 g000 = vec3(gx0.x, gy0.x, gz0.x);
    vec3 g100 = vec3(gx0.y, gy0.y, gz0.y);
    vec3 g010 = vec3(gx0.z, gy0.z, gz0.z);
    vec3 g110 = vec3(gx0.w, gy0.w, gz0.w);
    vec3 g001 = vec3(gx1.x, gy1.x, gz1.x);
    vec3 g101 = vec3(gx1.y, gy1.y, gz1.y);
    vec3 g011 = vec3(gx1.z, gy1.z, gz1.z);
    vec3 g111 = vec3(gx1.w, gy1.w, gz1.w);

    vec4 norm0 = taylor_inv_sqrt(vec4(dot(g000, g000), dot(g010, g010), dot(g100, g100), dot(g110, g110)));
    g000 *= norm0.x;
    g010 *= norm0.y;
    g100 *= norm0.z;
    g110 *= norm0.w;
    vec4 norm1 = taylor_inv_sqrt(vec4(dot(g001, g001), dot(g011, g011), dot(g101, g101), dot(g111, g111)));
    g001 *= norm1.x;
    g011 *= norm1.y;
    g101 *= norm1.z;
    g111 *= norm1.w;

    float n000 = dot(g000, Pf0);
    float n100 = dot(g100, vec3(Pf1.x, Pf0.yz));
    float n010 = dot(g010, vec3(Pf0.x, Pf1.y, Pf0.z));
    float n110 = dot(g110, vec3(Pf1.xy, Pf0.z));
    float n001 = dot(g001, vec3(Pf0.xy, Pf1.z));
    float n101 = dot(g101, vec3(Pf1.x, Pf0.y, Pf1.z));
    float n011 = dot(g011, vec3(Pf0.x, Pf1.yz));
    float n111 = dot(g111, Pf1);

    vec3 f = fade_curve(Pf0);
    vec4 nz = mix(vec4(n000, n100, n010, n110), vec4(n001, n101, n011, n111), f.z);
    vec2 ny = mix(nz.xy, nz.zw, f.y);
    return 2.2 * mix(ny.x, ny.y, f.x);
}

float fbm(vec3 p) {
    return (cnoise(p) + 0.5 * cnoise(p * 2.0)) / 1.5;
}

float sd_isosceles(vec2 point, vec2 q) {
    vec2 p = vec2(abs(point.x), point.y);
    vec2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0, 1.0);
    vec2 b = p - q * vec2(clamp(p.x / q.x, 0.0, 1.0), 1.0);
    float s = -sign(q.y);
    vec2 d = min(vec2(dot(a, a), s * (p.x * q.y - p.y * q.x)), vec2(dot(b, b), s * (p.y - q.y)));
    return -sqrt(d.x) * sign(d.y);
}

float shape_distance(vec2 p, int shape, float c) {
    if (shape == 0) return max(abs(p.x), abs(p.y)) - c;
    if (shape == 1) return length(p) - c;
    return sd_isosceles(vec2(p.x, p.y + c), vec2(c, 2.0 * c));
}

// Glyph of a cell: shape index and size.
void glyph(vec2 center, vec2 drift, float field_scale, float bias, float t, out int shape, out float size) {
    float noise = fbm(vec3((center + drift) / field_scale + SEED, t));
    float tone = clamp((noise * 0.5 + 0.5 - bias) * 2.8 * P_CONTRAST + 0.5, 0.0, 1.0);
    float stepped = floor(min(tone, 0.999999) * 3.0);
    shape = 2 - int(stepped);
    size = P_DOT_SIZE;
#if P_SHAPES != 0
    shape = P_SHAPES - 1;
    size = P_DOT_SIZE * mix(0.45, 1.0, stepped / 2.0);
#endif
}

// Glyph area in px^2 for a band (0 dark .. 2 bright).
float band_area(float stepped, float cell_px) {
    float size = P_DOT_SIZE;
    float k = stepped < 0.5 ? 2.0 : (stepped < 1.5 ? 3.14159 : 4.0);
#if P_SHAPES != 0
    size = P_DOT_SIZE * mix(0.45, 1.0, stepped / 2.0);
    k = P_SHAPES == 1 ? 4.0 : (P_SHAPES == 2 ? 3.14159 : 2.0);
#endif
    float h = size * cell_px * 0.5;
    return k * h * h;
}

float fade_level(vec2 uv) {
    if (P_FADE <= 0.0) return 1.0;
    vec2 q = abs(uv * 2.0 - 1.0);
    float radius = pow(pow(q.x, 2.5) + pow(q.y, 2.5), 1.0 / 2.5) / pow(2.0, 1.0 / 2.5);
    return 1.0 - smoothstep(max(0.0, 1.0 - P_FADE * 2.2), 1.0, radius);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    // WebGPU uv: top-left origin.
    vec2 pixel = vec2(frag.x, res.y - frag.y);
    vec2 uv = pixel / res;
    vec3 background = P_BACKGROUND;

    float cols = max(1.0, floor(res.x / P_CELL_SIZE + 0.5));
    float cell_px = res.x / cols;
    float rows = max(1.0, floor(res.y / cell_px));
    vec2 origin = vec2(0.0, (res.y - rows * cell_px) * 0.5);

    // time += dt * 0.1 * speed; drift += flow * cellPx * dt along direction.
    float t = wl_time() * 0.1;
    float dir = radians(P_DIRECTION);
    vec2 drift = vec2(cos(dir), sin(dir)) * P_FLOW * cell_px * wl_time();
    float field_scale = 32.0 * cell_px * P_SCALE;
    float bias = 0.5 - (P_BRIGHTNESS - 0.5) * 0.4;

    vec3 rgb = background;
    vec2 cell = floor((pixel - origin) / cell_px);
    float level = fade_level(uv);
    vec2 center = origin + (cell + 0.5) * cell_px;
    int shape;
    float size;
    glyph(center, drift, field_scale, bias, t, shape, size);
    if (cell.y >= 0.0 && cell.y < rows && cell.x >= 0.0 && cell.x < cols) {
        vec2 local = (pixel - center) / (cell_px * 0.5);
        float aa = 2.0 / cell_px;
        float coverage = smoothstep(aa, -aa, shape_distance(local, shape, size));
        rgb = mix(background, P_COLOR, coverage * level);
    }

    // Bloom: only glyphs whose faded colour is brighter than the threshold
    // glow, so with the default grey the pass contributes nothing.
    float lum = dot(P_COLOR, vec3(0.2126, 0.7152, 0.0722));
    if (P_GLOW > 0.0 && lum > GLOW_THRESHOLD) {
        // Two 1D gaussians of sigma 4 half-res texels = 8 px. The blur
        // spans about one cell, so it mostly sees the local glyph energy:
        // evaluate the wave at the pixel itself (continuous, no per-cell
        // steps), blend the three band areas across the band edges, and
        // spread it over the neighbouring cell centres.
        float sigma = 8.0;
        float noise_px = fbm(vec3((pixel + drift) / field_scale + SEED, t));
        float tone_px = clamp((noise_px * 0.5 + 0.5 - bias) * 2.8 * P_CONTRAST + 0.5, 0.0, 1.0);
        float b1 = smoothstep(0.333 - 0.04, 0.333 + 0.04, tone_px);
        float b2 = smoothstep(0.667 - 0.04, 0.667 + 0.04, tone_px);
        float area = mix(mix(band_area(0.0, cell_px), band_area(1.0, cell_px), b1), band_area(2.0, cell_px), b2);
        // A glyph blurred by the gaussian ~ a gaussian whose variance also
        // carries the glyph's own extent (keeps the energy, widens big ones).
        float s2 = sigma * sigma + area / 6.2831853;
        vec3 bloom = vec3(0.0);
        for (int j = -2; j <= 2; j++) {
            for (int i = -2; i <= 2; i++) {
                vec2 c = cell + vec2(float(i), float(j));
                if (c.y < 0.0 || c.y >= rows || c.x < 0.0 || c.x >= cols) continue;
                vec2 ctr = origin + (c + 0.5) * cell_px;
                vec2 d = pixel - ctr;
                float r2 = dot(d, d);
                if (r2 > 9.0 * s2) continue;
                float lv = fade_level(ctr / res);
                float g = max(0.0, (lum * lv - GLOW_THRESHOLD) / (1.0 - GLOW_THRESHOLD));
                vec3 glyph_rgb = mix(background, P_COLOR, lv);
                bloom += glyph_rgb * g * area * exp(-r2 / (2.0 * s2));
            }
        }
        bloom /= 6.2831853 * s2;
        rgb += bloom * 2.0 * P_GLOW;
    }
    gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
