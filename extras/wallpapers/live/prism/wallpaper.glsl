// Prism — ported from React Bits <Prism /> (see ../LICENSE-REACTBITS.md).
// Same volumetric march as the component (it renders at half resolution;
// this runs at full resolution with fewer, longer steps). Cursor tilt and the
// intro fade are dropped; the custom palette texture is evaluated inline.

#define STEPS 60
#define STEP_SCALE (100.0 / float(STEPS))

const vec4 PHASE_COS = vec4(1.0, 0.5403023058681398, -0.4161468365471424, -0.9899924966004454);
const vec4 PHASE_SIN = vec4(0.0, 0.8414709848078965, 0.9092974268256817, 0.1411200080598672);

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453123);
}

float pyramid(vec3 p) {
    float half_base = P_BASE_WIDTH * 0.5;
    vec3 q = vec3(abs(p.x) / half_base, abs(p.y) / P_HEIGHT, abs(p.z) / half_base);
    return max((q.x + q.y + q.z - 1.0) * min(half_base, P_HEIGHT) * 0.5773502691896258, -p.y);
}

vec3 stop(int i) {
    if (i == 0) return P_COLOR_1;
    if (i == 1) return P_COLOR_2;
    return P_COLOR_3;
}

// The component's 64-texel cyclic palette (smoothstep between stops).
vec3 palette(float phase) {
    float position = fract(phase * 0.15915494309189535) * 3.0;
    float index = floor(position);
    float t = position - index;
    float k = t * t * (3.0 - 2.0 * t);
    vec3 a = P_COLOR_1;
    vec3 b = P_COLOR_2;
    if (index > 0.5) { a = P_COLOR_2; b = P_COLOR_3; }
    if (index > 1.5) { a = P_COLOR_3; b = P_COLOR_1; }
    return mix(a, b, k);
}

vec3 grade(vec3 col, vec2 seed) {
    col += (rand(seed) - 0.5) * P_NOISE;
    col = clamp(col, 0.0, 1.0);
    float L = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = clamp(mix(vec3(L), col, P_SATURATION), 0.0, 1.0);
    if (abs(P_HUE_SHIFT) > 0.0001) {
        float h = radians(P_HUE_SHIFT);
        float c = cos(h);
        float s = sin(h);
        mat3 hue = mat3(
            0.299 + 0.701 * c + 0.168 * s, 0.587 - 0.587 * c - 0.331 * s, 0.114 - 0.114 * c + 0.5 * s,
            0.299 - 0.299 * c + 0.328 * s, 0.587 + 0.413 * c + 0.035 * s, 0.114 - 0.114 * c - 0.5 * s,
            0.299 - 0.3 * c - 0.497 * s, 0.587 - 0.588 * c + 0.296 * s, 0.114 + 0.886 * c + 0.201 * s);
        col = clamp(hue * col, 0.0, 1.0);
    }
    return col;
}

mat3 rotation(float yaw, float pitch, float roll) {
    float cy = cos(yaw);
    float sy = sin(yaw);
    float cx = cos(pitch);
    float sx = sin(pitch);
    float cz = cos(roll);
    float sz = sin(roll);
    return mat3(
        cy * cz + sy * sx * sz, cx * sz, -sy * cz + cy * sx * sz,
        -cy * sz + sy * sx * cz, cx * cz, sy * sz + cy * sx * cz,
        sy * cx, -sx, cy * cx);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float time = wl_time();
    // The component spins with time * timeScale (default 0.5).
    float t = time * 0.5;

#if P_ANIMATION == 0
    float c0 = cos(t);
    float c1 = cos(t + 33.0);
    float c2 = cos(t + 11.0);
    mat3 ray = mat3(c0, 0.0, c2, 0.0, 1.0, 0.0, c1, 0.0, c0);
#elif P_ANIMATION == 1
    mat3 ray = rotation(t * 0.55, sin(t * 0.6 + 0.6) * 0.6, sin(t * 0.35 + 1.9) * 0.5);
#else
    mat3 ray = mat3(1.0);
#endif

    vec2 f = (frag - 0.5 * res - vec2(P_OFFSET_X, -P_OFFSET_Y) * res) / (res.y * 0.1 * P_SCALE);
    vec3 origin = ray * vec3(f, 0.0);
    vec3 axis = ray[2];
    vec3 shift = vec3(0.0, P_HEIGHT * 0.25, 0.0);
    float z = 5.0;
    vec4 o = vec4(0.0);
    for (int i = 0; i < STEPS; i++) {
        vec3 p = origin + axis * z;
        float d = 0.1 + 0.2 * abs(pyramid(p + shift));
        z -= d * STEP_SCALE;
        float phase = (p.y + z) * P_COLOR_FREQUENCY;
        vec4 band = sin(phase) * PHASE_COS + cos(phase) * PHASE_SIN + 1.0;
#if P_CUSTOM_COLORS
        band.rgb = palette(phase) * 2.0;
#endif
        o += band / d;
    }
    o *= STEP_SCALE;
    vec4 raw = o * o * P_GLOW / 1e5;
    vec4 e = exp(2.0 * min(raw, 20.0));
    vec4 mapped = (e - 1.0) / (e + 1.0);
    vec2 seed = floor(frag) + 0.5 + vec2(time);

#if P_LIGHT_MODE
#if P_CUSTOM_COLORS
    float sat = 0.0;
    for (int i = 0; i < 3; i++) {
        vec3 c = stop(i);
        float top = max(c.r, max(c.g, c.b));
        sat += top > 0.0 ? (top - min(c.r, min(c.g, c.b))) / top : 0.0;
    }
    float neutral = 1.0 - sat / 3.0;
#else
    float neutral = 0.0;
#endif
    float peak = max(raw.r, max(raw.g, raw.b));
    vec3 hue = grade(raw.rgb / max(peak, 1e-6), seed);
    vec3 ink = pow(hue, vec3(1.5)) * mix(0.86, 0.32, neutral);
    float strength = max(mapped.r, max(mapped.g, mapped.b)) * mapped.a;
    float cover = clamp(strength * 0.95 + (rand(seed.yx) - 0.5) * P_NOISE * 0.5, 0.0, 1.0);
    vec3 page = vec3(1.0) - P_BACKGROUND * 0.35;
    gl_FragColor = vec4(wl_over(page, vec4(ink * cover, cover)), 1.0);
#else
    vec4 premul = vec4(grade(mapped.rgb, seed) * mapped.a, mapped.a);
    gl_FragColor = vec4(wl_over(P_BACKGROUND, premul), 1.0);
#endif
}
