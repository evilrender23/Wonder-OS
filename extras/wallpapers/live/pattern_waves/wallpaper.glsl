// PatternWaves — ported from React Bits <PatternWaves /> (see ../LICENSE-REACTBITS.md).
// Single pass: the per-cell field texture is evaluated on demand for the two
// rows that can reach the pixel, with an analytic gradient noise instead of
// five finite-difference height samples. Glyphs are drawn as small SDF
// characters instead of a font atlas. Cursor ripples and intro are dropped.

const float FOLDS = 5.5;
const float WAVE_UNIT = 520.0;

vec3 lattice(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx) * 2.0 - 1.0;
}

// Quintic gradient noise with its derivative: (value, d/dx, d/dy, d/dz).
vec4 noised(vec3 x) {
    vec3 i = floor(x);
    vec3 f = x - i;
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec3 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    vec3 ga = lattice(i + vec3(0.0, 0.0, 0.0));
    vec3 gb = lattice(i + vec3(1.0, 0.0, 0.0));
    vec3 gc = lattice(i + vec3(0.0, 1.0, 0.0));
    vec3 gd = lattice(i + vec3(1.0, 1.0, 0.0));
    vec3 ge = lattice(i + vec3(0.0, 0.0, 1.0));
    vec3 gf = lattice(i + vec3(1.0, 0.0, 1.0));
    vec3 gg = lattice(i + vec3(0.0, 1.0, 1.0));
    vec3 gh = lattice(i + vec3(1.0, 1.0, 1.0));
    float va = dot(ga, f - vec3(0.0, 0.0, 0.0));
    float vb = dot(gb, f - vec3(1.0, 0.0, 0.0));
    float vc = dot(gc, f - vec3(0.0, 1.0, 0.0));
    float vd = dot(gd, f - vec3(1.0, 1.0, 0.0));
    float ve = dot(ge, f - vec3(0.0, 0.0, 1.0));
    float vf = dot(gf, f - vec3(1.0, 0.0, 1.0));
    float vg = dot(gg, f - vec3(0.0, 1.0, 1.0));
    float vh = dot(gh, f - vec3(1.0, 1.0, 1.0));
    float k0 = va;
    float k1 = vb - va;
    float k2 = vc - va;
    float k3 = ve - va;
    float k4 = va - vb - vc + vd;
    float k5 = va - vc - ve + vg;
    float k6 = va - vb - ve + vf;
    float k7 = -va + vb + vc - vd + ve - vf - vg + vh;
    float value = k0 + u.x * k1 + u.y * k2 + u.z * k3 + u.x * u.y * k4 + u.y * u.z * k5
        + u.z * u.x * k6 + u.x * u.y * u.z * k7;
    vec3 grad = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga)
        + u.x * u.y * (ga - gb - gc + gd) + u.y * u.z * (ga - gc - ge + gg)
        + u.z * u.x * (ga - gb - ge + gf) + u.x * u.y * u.z * (-ga + gb + gc - gd + ge - gf - gg + gh)
        + du * (vec3(k1, k2, k3) + u.yzx * vec3(k4, k5, k6) + u.zxy * vec3(k6, k4, k5)
                + u.yzx * u.zxy * k7);
    return vec4(value, grad);
}

// Surface height and its gradient in wave units: (h, dh/dx, dh/dy).
vec3 surface(vec2 p, float t, vec2 heading) {
    if (P_WAVE == 0) {
        vec2 side = vec2(-heading.y, heading.x);
        float u = dot(p, heading);
        float v = dot(p, side);
        vec4 n1 = noised(vec3(v * 0.85, u * 0.3, t * 0.05));
        float bend = n1.x * 1.7 + 0.4 * sin(v * 1.6 + t * 0.2);
        vec2 dbend = 1.7 * (n1.y * 0.85 * side + n1.z * 0.3 * heading)
            + 0.64 * cos(v * 1.6 + t * 0.2) * side;
        float phase = u * FOLDS + bend - t * 0.45;
        vec2 dphase = FOLDS * heading + dbend;
        vec4 n2 = noised(vec3(u * 0.55 + 3.0, v * 0.45, t * 0.04));
        float swell = 0.6 + 0.4 * n2.x;
        vec2 dswell = 0.4 * (n2.y * 0.55 * heading + n2.z * 0.45 * side);
        float fold = sin(phase) + 0.32 * sin(2.0 * phase + 1.3);
        vec2 dfold = (cos(phase) + 0.64 * cos(2.0 * phase + 1.3)) * dphase;
        float arg = u * FOLDS * 2.5 + bend * 1.9 - t * 0.9 + 2.1;
        float ripple = 0.16 * sin(arg);
        vec2 dripple = 0.16 * cos(arg) * (FOLDS * 2.5 * heading + 1.9 * dbend);
        float h = (fold + ripple) * swell;
        return vec3(h, (dfold + dripple) * swell + (fold + ripple) * dswell);
    }
    if (P_WAVE == 1) {
        vec4 n = noised(vec3(p * 0.6, t * 0.05));
        float bend = n.x * 0.6;
        vec2 dbend = 0.36 * n.yz;
        float phase = dot(p, heading) * 15.0 + bend * 2.2 - t * 1.4;
        vec2 dphase = 15.0 * heading + 2.2 * dbend;
        float sw = sin(phase) + 0.3 * sin(2.0 * phase - 0.8);
        vec2 dsw = (cos(phase) + 0.6 * cos(2.0 * phase - 0.8)) * dphase;
        vec4 n2 = noised(vec3(p * 0.9 + 11.0, t * 0.05));
        float roll = 0.75 + 0.25 * n2.x;
        vec2 droll = 0.225 * n2.yz;
        return vec3(0.8 * sw * roll, 0.8 * (dsw * roll + sw * droll));
    }
    vec2 q = p + heading * 1.2;
    float r = max(length(q), 1e-4);
    vec2 dr = q / r;
    vec4 n = noised(vec3(p * 1.2, t * 0.05));
    float bend = n.x * 0.1;
    vec2 dbend = 0.12 * n.yz;
    float arg = (r + bend) * 9.0 - t * 1.8;
    float e = exp(-(r - 0.6) * 0.8);
    float env = 0.45 + 0.55 * e;
    vec2 denv = -0.44 * e * dr;
    return vec3(sin(arg) * env, cos(arg) * 9.0 * (dr + dbend) * env + sin(arg) * denv);
}

float lum(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// The field pass for one cell centre: (level, alpha, emphasis, lift) plus the
// horizontal slope of the lift in pixels per pixel (for the line pattern).
vec4 field(vec2 css, vec2 res, float t, vec2 heading, vec3 lightDir, float ink, out float slope) {
    float unit = WAVE_UNIT * P_SCALE;
    vec3 s = surface((css - 0.5 * res) / unit, t, heading);
    float h = s.x;
    vec2 grad = s.yz * P_DEPTH * 0.4;
    vec3 n = normalize(vec3(-grad, 1.0));

    float diffuse = clamp(dot(n, lightDir), 0.0, 1.0);
    vec3 halfway = normalize(lightDir + vec3(0.0, 0.0, 1.0));
    float spec = pow(clamp(dot(n, halfway), 0.0, 1.0), 160.0) * P_SHINE * 1.15;
    float hollow = 0.7 + 0.3 * clamp(h * 0.5 + 0.5, 0.0, 1.0);
    float tone = clamp(diffuse * hollow * 0.78 + spec, 0.0, 1.0);
    tone = clamp((tone - 0.42) * P_CONTRAST + 0.42, 0.0, 1.0);

    float level = ink > 0.5 ? pow(clamp(1.0 - tone / 0.46, 0.0, 1.0), 2.4) * 0.72 : pow(tone, 2.2);
    float emphasis = ink > 0.5 ? smoothstep(0.55, 0.95, level) : clamp(spec * 1.6, 0.0, 1.0);

    float fade = 1.0;
    vec2 uv = css / res;
    vec2 c = uv * 2.0 - 1.0;
    if (P_FADE == 0) {
        fade = 1.0 - smoothstep(1.0 - P_FADE_SIZE, 1.18, length(c));
    } else if (P_FADE == 1) {
        fade = mix(0.05, 1.0, smoothstep(0.08, 0.08 + P_FADE_SIZE, length(c * vec2(1.0, 1.35))));
    } else if (P_FADE == 2) {
        fade = smoothstep(0.0, P_FADE_SIZE, uv.y);
    } else if (P_FADE == 3) {
        fade = smoothstep(0.0, P_FADE_SIZE, 1.0 - uv.y);
    }

    float alpha = fade * (0.22 + 0.78 * level);
    float raw = h * P_DEPTH * 0.42;
    float lift = clamp(raw, -0.48, 0.48);
    slope = abs(raw) < 0.48 ? P_DEPTH * 0.42 * s.y / unit : 0.0;
    return vec4(level, alpha, emphasis, lift);
}

float box(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float seg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// '.:-=+*#%@' as distance fields in tile units (tile = [-0.5, 0.5]^2).
float glyph(vec2 g, float index) {
    float w = 0.045;
    if (index < 0.5) return length(g - vec2(0.0, -0.2)) - 0.065;
    if (index < 1.5) return min(length(g - vec2(0.0, -0.2)), length(g - vec2(0.0, 0.12))) - 0.065;
    if (index < 2.5) return seg(g, vec2(-0.19, 0.0), vec2(0.19, 0.0)) - w;
    if (index < 3.5) return min(seg(g, vec2(-0.2, 0.09), vec2(0.2, 0.09)), seg(g, vec2(-0.2, -0.09), vec2(0.2, -0.09))) - w;
    if (index < 4.5) return min(seg(g, vec2(-0.21, 0.0), vec2(0.21, 0.0)), seg(g, vec2(0.0, -0.21), vec2(0.0, 0.21))) - w;
    if (index < 5.5) {
        vec2 c = g - vec2(0.0, 0.08);
        float d = seg(c, vec2(0.0, -0.17), vec2(0.0, 0.17));
        d = min(d, seg(c, vec2(-0.15, -0.085), vec2(0.15, 0.085)));
        d = min(d, seg(c, vec2(-0.15, 0.085), vec2(0.15, -0.085)));
        return d - w;
    }
    if (index < 6.5) {
        float d = seg(g, vec2(-0.06, -0.28), vec2(-0.02, 0.28));
        d = min(d, seg(g, vec2(0.06, -0.28), vec2(0.1, 0.28)));
        d = min(d, seg(g, vec2(-0.22, 0.09), vec2(0.24, 0.09)));
        d = min(d, seg(g, vec2(-0.24, -0.09), vec2(0.22, -0.09)));
        return d - w * 0.9;
    }
    if (index < 7.5) {
        float d = seg(g, vec2(-0.21, -0.27), vec2(0.21, 0.27)) - w * 0.9;
        d = min(d, abs(length(g - vec2(-0.13, 0.15)) - 0.075) - w * 0.8);
        d = min(d, abs(length(g - vec2(0.13, -0.15)) - 0.075) - w * 0.8);
        return d;
    }
    float d = abs(length(g) - 0.25) - w;
    d = max(d, -(box(g - vec2(0.17, -0.1), vec2(0.09, 0.06), 0.0)));
    d = min(d, abs(length(g - vec2(0.0, 0.0)) - 0.1) - w);
    d = min(d, seg(g, vec2(0.1, 0.1), vec2(0.12, -0.08)) - w);
    return d;
}

float coverage(vec2 local, float level, float pitch, float stroke) {
    float span = pitch * P_MARK_SIZE;
    float area = max(sqrt(level), 0.14);
    if (P_PATTERN == 0) {
        return clamp(0.5 - (length(local) - 0.5 * span * area), 0.0, 1.0);
    }
    if (P_PATTERN == 1) {
        float extent = 0.5 * span * area;
        return clamp(0.5 - box(local, vec2(extent), extent * 0.3), 0.0, 1.0);
    }
    if (P_PATTERN == 2) {
        float arm = 0.5 * span * mix(0.3, 1.0, level);
        float width = 0.5 * stroke;
        return clamp(0.5 - min(box(local, vec2(arm, width), width), box(local, vec2(width, arm), width)), 0.0, 1.0);
    }
    vec2 g = local / span;
    if (abs(g.x) > 0.5 || abs(g.y) > 0.5) return 0.0;
    float index = min(floor(level * 9.0), 8.0);
    return clamp(0.5 - glyph(g, index) * span, 0.0, 1.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    // Preset «silk» runs its surface at speed 0.35.
    float t = wl_time() * 0.35;

    float heading_a = radians(P_DIRECTION);
    vec2 heading = vec2(cos(heading_a), sin(heading_a));
    float la = radians(P_DIRECTION + 180.0 + P_LIGHT);
    vec3 lightDir = vec3(cos(la) * 0.78, sin(la) * 0.78, 0.62);
    float ink = lum(P_COLOR) < lum(P_BACKGROUND) ? 1.0 : 0.0;
    vec3 accent = P_COLOR + ((ink > 0.5 ? 0.0 : 1.0) - P_COLOR) * (ink > 0.5 ? 0.35 : 0.55);

    float pitchY = max(4.0, floor(P_SPACING + 0.5));
    float pitchX = P_PATTERN == 3 ? max(2.0, floor(pitchY / 4.0 + 0.5)) : pitchY;
    float cols = ceil(res.x / pitchX) + 1.0;
    float rows = ceil(res.y / pitchY) + 2.0;
    vec2 origin = floor((res - vec2(cols * pitchX, rows * pitchY)) * 0.5);
    vec2 rel = frag - origin;
    float stroke = P_PATTERN == 3 ? 0.9 : max(1.1, pitchY * 0.08);

    float row = floor(rel.y / pitchY);
    float fy = rel.y / pitchY - row;
    float other = fy > 0.5 ? row + 1.0 : row - 1.0;

    vec4 inkc = vec4(0.0);
    for (int k = 0; k < 2; k++) {
        float cy = k == 0 ? row : other;
        float slope;
        if (P_PATTERN == 3) {
            // Contour lines: sampled at the pixel's x (the original
            // interpolates columns a quarter pitch apart).
            vec2 css = origin + vec2(rel.x, (cy + 0.5) * pitchY);
            vec4 f = field(css, res, t, heading, lightDir, ink, slope);
            if (f.y < 0.002) continue;
            float y = (cy + 0.5 + f.w) * pitchY;
            float s = slope * pitchY;
            float thickness = max(stroke, pitchY * P_MARK_SIZE * f.x);
            float d = abs(rel.y - y) / sqrt(1.0 + s * s) - 0.5 * thickness;
            float alpha = clamp(0.5 - d, 0.0, 1.0) * f.y;
            if (alpha > inkc.a) inkc = vec4(mix(P_COLOR, accent, f.z) * alpha, alpha);
        } else {
            float cx = floor(rel.x / pitchX);
            vec2 css = origin + (vec2(cx, cy) + 0.5) * vec2(pitchX, pitchY);
            vec4 f = field(css, res, t, heading, lightDir, ink, slope);
            if (f.y < 0.002) continue;
            vec2 center = (vec2(cx, cy) + 0.5) * vec2(pitchX, pitchY) + vec2(0.0, f.w * pitchY);
            float alpha = coverage(rel - center, f.x, pitchY, stroke) * f.y;
            if (alpha > inkc.a) inkc = vec4(mix(P_COLOR, accent, f.z) * alpha, alpha);
        }
    }
    gl_FragColor = vec4(wl_over(P_BACKGROUND, inkc), 1.0);
}
