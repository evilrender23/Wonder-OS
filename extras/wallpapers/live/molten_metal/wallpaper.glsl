// Molten Metal — ported from React Bits <MoltenMetal /> (see ../LICENSE-REACTBITS.md).
// Same folded-filament field and palette as the original. The cursor heat
// trail, click flares, cursor drift and the ignition intro are dropped.

vec3 color_at(int i) {
    // Stops: color_1..color_N, then the core colour.
    if (i >= P_COLOR_COUNT) return P_CORE_COLOR;
    if (i == 0) return P_COLOR_1;
    if (i == 1) return P_COLOR_2;
    if (i == 2) return P_COLOR_3;
    if (i == 3) return P_COLOR_4;
    return P_COLOR_5;
}

#if P_COLOR_MODE == 1
const float BALANCE = 0.3;
#elif P_COLOR_MODE == 2
const float BALANCE = 0.7;
#else
const float BALANCE = 0.5;
#endif

vec3 palette(float g) {
    float cnt = float(P_COLOR_COUNT + 1);
    float t = g < BALANCE ? 0.5 * g / BALANCE : 0.5 + 0.5 * (g - BALANCE) / (1.0 - BALANCE);
    float scaled = t * (cnt - 1.0);
    float fi = min(floor(scaled), cnt - 2.0);
    int index = int(fi);
    return mix(color_at(index), color_at(index + 1), smoothstep(0.0, 1.0, scaled - fi));
}

void main() {
    float time = wl_time() * 0.35 + 0.4;
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float glow = P_GLOW * 0.1;
    float thickness = clamp(P_THICKNESS, 0.2, 3.0);
    float fold = -P_TURBULENCE;
    float black_point = 0.2 * (1.0 - clamp(P_HAZE, 0.0, 1.0));
    float detail = clamp(P_DETAIL, 1.0, 8.0);
    float rad = P_ROTATION * 3.14159265 / 180.0;
    vec2 rotv = vec2(cos(rad), sin(rad));

    vec2 uv = (frag - 0.5 * res) / res.y;
    vec2 q = uv - vec2(P_OFFSET_X * res.x / res.y, -P_OFFSET_Y);
    vec2 view = vec2(q.x * rotv.x - q.y * rotv.y, q.x * rotv.y + q.y * rotv.x);
    vec2 p = max(P_SCALE, 0.1) * view - 0.5;

    float power = thickness;

    vec2 i = p;
    float c = 0.0;
    float r = length(p + vec2(sin(time), sin(time * 0.3 + 5.0)) * 0.5);
    float d = length(p);
    float rot = d + time + p.x * P_SWIRL;
    float cosRot = cos(rot);
    mat2 warp = mat2(cos(rot - sin(time / 5.0)), sin(rot), -sin(cosRot - time), cosRot) * fold;
    float layers = ceil(detail);

    for (int k = 0; k < 8; k++) {
        float n = float(k);
        if (n >= layers) break;
        p *= warp;
        float t = r - time / (n + 3.0);
        i -= p + vec2(cos(t - i.x - r) + sin(t + i.y), sin(t - i.y) + cos(t + i.x) + r);
        float l = length(vec2(sin(i.x + t), cos(i.y + t)));
        float footprint = 0.5 * length(vec2(dFdx(l), dFdy(l)));
        l = sqrt(l * l + footprint * footprint);
        float weight = clamp(detail - n, 0.0, 1.0);
        c += weight * glow / (power == 1.0 ? l : pow(max(l, 1e-6), power));
    }

    c /= 6.0;

    float g = clamp(max(c - black_point, 0.0) * P_BRIGHTNESS, 0.0, 1.0);
    vec3 col = palette(g);
    float noise = wl_hash12(frag + fract(wl_time()) * 97.0) - 0.5;
    float a;
#if P_LIGHT_MODE
    float lift = max(g - 0.03, 0.0) / 0.97;
    a = (1.0 - exp(-6.0 * lift)) / (1.0 - exp(-6.0)) + noise * P_GRAIN * g;
#else
    a = g + noise * P_GRAIN;
#endif
    a = clamp(a + noise / 255.0, 0.0, 1.0);
    gl_FragColor = vec4(wl_over(P_BACKGROUND, vec4(col * a, a)), 1.0);
}
