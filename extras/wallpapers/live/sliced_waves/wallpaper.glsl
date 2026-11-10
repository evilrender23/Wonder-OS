// SlicedWaves — ported from React Bits <SlicedWaves /> (see ../LICENSE-REACTBITS.md).
// Single pass: the instanced bars are evaluated per pixel. The bar under the
// pixel is found by inverting the strip's perspective projection (Newton on
// the bent strip), then the few neighbouring bars × ribbons are summed
// additively like the original's ONE/ONE blend, followed by the composite
// tone map. Cursor focus/lift and the intro are dropped.

const float TAU = 6.28318530718;
const float FOCAL = 1.1;
const float DIST = 2.6;

float hash1(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

float noise1(float x) {
    float i = floor(x);
    float f = x - i;
    float u = f * f * (3.0 - 2.0 * f);
    return mix(hash1(i), hash1(i + 1.0), u);
}

float hash2(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float smootherEdge(float edge, float x) {
    float t = clamp(x / max(edge, 0.0001), 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float erfApprox(float x) {
    float a = x * x;
    float t = 1.0 - exp(-a * (1.2732395 + 0.147 * a) / (1.0 + 0.147 * a));
    return sign(x) * sqrt(max(t, 0.0));
}

float box(float d, float h, float s) {
    float k = 0.70710678 / max(s, 0.3);
    return 0.5 * (erfApprox((h - d) * k) + erfApprox((h + d) * k));
}

vec3 paletteAt(float i) {
    if (i < 0.5) return pow(P_COLOR1, vec3(2.2));
    if (i < 1.5) return pow(P_COLOR2, vec3(2.2));
    return pow(P_COLOR3, vec3(2.2));
}

vec3 palette(float x) {
    float f = mod(x, 3.0);
    float i = floor(f);
    float m = f - i;
    m = m * m * (3.0 - 2.0 * m);
    return mix(paletteAt(i), paletteAt(mod(i + 1.0, 3.0)), m);
}

// Sum of the bars i = ic-7 .. ic+7 (all ribbons) at the unrotated pixel q.
vec3 sumBars(float ic, vec2 q, vec2 res, float t, float dens) {
    float w = res.x;
    float h = res.y;
    float F = h * FOCAL;
    float centerScale = F / DIST;
    float pitch = P_SPACING / centerScale;
    float halfBar = pitch * P_BAR_WIDTH * 0.5;
    float yaw = P_PERSPECTIVE * 0.66;
    float cy = cos(yaw);
    float sy = sin(yaw);
    float bend = P_CURVE * 0.6;
    float reach = ceil((max(w, h) / F) * DIST * 2.4 / pitch);
    float thickness = (P_RIBBON_HEIGHT * h * 0.5) / centerScale;
    float separation = (P_SPREAD * h) / centerScale;
    float amplitude = (P_AMPLITUDE * h) / centerScale;
    float frequency = 0.22 * P_FREQUENCY;
    float base = h * P_POSITION;
    float blurScale = P_BLUR * 16.0;
    float ribbons = float(P_RIBBONS);
#if P_LIGHT_MODE
    float glowAmt = P_GLOW * 0.4;
    float blurMode = 0.55;
#else
    float glowAmt = P_GLOW;
    float blurMode = 1.0;
#endif
    vec3 light = vec3(0.0);
    for (int k = -7; k <= 7; k++) {
        float i = ic + float(k);
        if (abs(i) > reach) continue;
        float bx = i * pitch;
        float bl = bend * sin(bx * 0.9 + 0.8);
        float X = bx * cy - bl * sy;
        float Z = bx * sy + bl * cy + DIST;
        if (Z < 0.45) continue;
        float scale = F / Z;
        float sx = 0.5 * w + X * scale;
        float hw = halfBar * scale;
        float lx = q.x - sx;
        float blurPx = (0.5 + P_BLUR * 0.6 + blurScale * abs(DIST / Z - 1.0)) * blurMode;
        float halo = hw + 5.0;
        float reachPad = max(blurPx * 2.5, halo * 2.0);
        if (abs(lx) > hw + reachPad) continue;
        float boxX = box(lx, hw, blurPx);
        float glowX = box(lx, hw, halo);
        float core = exp(-pow(lx / max(hw + blurPx * 0.5, 0.5), 2.0) * 1.5);
        for (int r = 0; r < 6; r++) {
            if (r >= P_RIBBONS) break;
            float fr = float(r);
            float offset = (fr - (ribbons - 1.0) / 2.0) * separation;
            float wave = 0.62 * sin(TAU * frequency * bx - t * 1.6 + fr * 2.1)
                + 0.38 * sin(TAU * frequency * 1.9 * bx + t * 2.25 + fr * 1.3);
            float yc = offset + wave * amplitude;
            float swell = 0.5 + 0.5 * sin(TAU * frequency * 0.7 * bx + t * 1.1 + fr * 2.7);
            float hh = thickness * (0.6 + 0.4 * swell) * scale;
            if (hh < 0.4) continue;
            float ly = q.y - (base - yc * scale);
            if (abs(ly) > hh + reachPad) continue;
            float tone = fr * 1.15 + bx * 0.3 + t * 0.12;
            vec3 top = palette(tone - 0.22);
            vec3 bottom = palette(tone + 0.22);
            float level = 0.75 + 0.25 * noise1(i * 0.9 + fr * 7.7 - t * 1.2);

            float body = boxX * box(ly, hh, blurPx * 1.15);
            float glow = glowX * box(ly, hh, halo);
            float tt = clamp(ly / max(hh, 0.001) * 0.5 + 0.5, 0.0, 1.0);
            vec3 color = mix(top, bottom, tt);
            float v = clamp(ly / max(hh, 0.001), -1.0, 1.0);
            body *= (1.0 + 0.35 * core) * (1.0 - 0.35 * v * v * v * v);
            if (P_SLICES > 0) {
                float cell = scale / float(P_SLICES);
                float yy = (yc * scale - ly) / max(cell, 0.001);
                float fs = 1.0 - abs(fract(yy) - 0.5) * 2.0;
                float soft = clamp(blurPx / max(cell, 0.001) * 1.6, 0.04, 1.0);
                body *= smoothstep(0.28 - soft, 0.28 + soft, fs);
            }
            vec3 tint = color * color / max(max(color.r, max(color.g, color.b)), 0.05);
            light += (color * body + tint * glowAmt * glow * 0.16) * level;
        }
    }

    return light * dens;
}

// Newton solve of F*X(x) - u*Z(x) = 0 (bar x projecting onto column u).
float solveStrip(float x, float u, float F, float cy, float sy, float bend) {
    float lin = F * cy - u * sy;
    for (int n = 0; n < 6; n++) {
        float sn = sin(0.9 * x + 0.8);
        float cn = cos(0.9 * x + 0.8);
        float f = x * lin - bend * sn * (F * sy + u * cy) - u * DIST;
        float df = lin - 0.9 * bend * cn * (F * sy + u * cy);
        float dx = abs(df) > 1e-3 ? f / df : 0.0;
        x -= clamp(dx, -1.0, 1.0);
    }
    return x;
}

float stripResidual(float x, float u, float F, float cy, float sy, float bend) {
    float l = bend * sin(0.9 * x + 0.8);
    float X = x * cy - l * sy;
    float Z = x * sy + l * cy + DIST;
    return abs(F * X / max(Z, 0.45) - u);
}

// Where the strip folds away, bars crowd closer than the ±7 window covers:
// scale the window up to the bars the glow would really reach.
float density(float ic, float pitch, float F, float cy, float sy, float bend, float halfBar) {
    float bx = ic * pitch;
    float l = bend * sin(0.9 * bx + 0.8);
    float dl = 0.9 * bend * cos(0.9 * bx + 0.8);
    float X = bx * cy - l * sy;
    float Z = max(bx * sy + l * cy + DIST, 0.45);
    float dX = cy - dl * sy;
    float dZ = sy + dl * cy;
    float ds = abs(F * (dX * Z - X * dZ) / (Z * Z)) * pitch;
    float hw = halfBar * F / Z;
    float pad = max((0.5 + P_BLUR * 0.6 + P_BLUR * 16.0 * abs(DIST / Z - 1.0)) * 2.5, (hw + 5.0) * 2.0);
    return clamp((hw + pad) / max(7.5 * ds, 1e-3), 1.0, 6.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float t = wl_time();
    float w = res.x;
    float h = res.y;

    float F = h * FOCAL;
    float centerScale = F / DIST;
    float pitch = P_SPACING / centerScale;
    float halfBar = pitch * P_BAR_WIDTH * 0.5;
    float yaw = P_PERSPECTIVE * 0.66;
    float cy = cos(yaw);
    float sy = sin(yaw);
    float bend = P_CURVE * 0.6;
    float angle = radians(P_ROTATION);
    float reach = ceil((max(w, h) / F) * DIST * 2.4 / pitch);

    // Pixel in the component's (top-left, y-down) frame, unrotated.
    vec2 ptl = vec2(frag.x, h - frag.y) - 0.5 * res;
    float ca = cos(angle);
    float sa = sin(angle);
    vec2 q = vec2(ca * ptl.x + sa * ptl.y, -sa * ptl.x + ca * ptl.y) + 0.5 * res;

    // Strip coordinate x whose bar projects onto this column.
    float u = q.x - 0.5 * w;
    float lin = F * cy - u * sy;
    float x = solveStrip(u * DIST / (abs(lin) > 1e-3 ? lin : 1e-3), u, F, cy, sy, bend);
    // A strongly turned, bent strip can fold back over itself: look for a
    // second branch from the far end of the strip.
    float x2 = solveStrip(reach * pitch, u, F, cy, sy, bend);
    float ic = floor(x / pitch + 0.5);
    float ic2 = floor(x2 / pitch + 0.5);
    bool has2 = abs(ic2 - ic) > 14.0 && abs(ic2) <= reach && stripResidual(x2, u, F, cy, sy, bend) < 2.0;

    vec3 light = sumBars(ic, q, res, t, density(ic, pitch, F, cy, sy, bend, halfBar));
    if (has2) light += sumBars(ic2, q, res, t, density(ic2, pitch, F, cy, sy, bend, halfBar));

    // Composite.
    vec3 hdr = light * 1.6 * P_BRIGHTNESS;
    vec2 uv = frag / res;
    float mask = 1.0;
    if (P_FADE > 0.0) {
        float edge = P_FADE * 0.5;
        mask = smootherEdge(edge, uv.x) * smootherEdge(edge, 1.0 - uv.x) * smootherEdge(edge, uv.y) * smootherEdge(edge, 1.0 - uv.y);
    }
    float grain = (hash2(frag + fract(t * 7.31) * 113.0) - 0.5) * P_GRAIN;
    float peak = max(hdr.r, max(hdr.g, hdr.b));
    vec4 premul;
#if P_LIGHT_MODE
    vec3 hue = hdr / max(peak, 0.0001);
    float tone = dot(hue, vec3(0.2126, 0.7152, 0.0722));
    vec3 col = pow(clamp(hue * min(1.0, 0.66 / max(tone, 0.001)), 0.0, 1.0), vec3(1.0 / 2.2));
    float alpha = clamp((1.0 - exp(-peak * 1.1)) * 0.85 * (1.0 + grain), 0.0, 1.0) * mask;
    premul = vec4(col * alpha, alpha);
    vec3 bg = dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722)) > 0.5 ? P_BACKGROUND : vec3(1.0);
#else
    vec3 mapped = 1.0 - exp(-hdr);
    mapped = mix(mapped, vec3(1.0), smoothstep(1.1, 3.2, peak) * 0.75);
    float level = max(mapped.r, max(mapped.g, mapped.b));
    mapped = clamp(mapped + grain * (0.25 + level), 0.0, 1.0) * mask;
    premul = vec4(mapped, max(mapped.r, max(mapped.g, mapped.b)));
    vec3 bg = P_BACKGROUND;
#endif
    gl_FragColor = vec4(wl_over(bg, premul), 1.0);
}
