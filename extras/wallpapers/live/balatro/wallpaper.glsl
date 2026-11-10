// Balatro — ported from React Bits <Balatro /> (see ../LICENSE-REACTBITS.md).
// Single pass: the paint shader runs per pixel block (pixelSize) and the CRT
// screen pass is applied in place. Cursor stirring, click twirls and the
// intro are dropped.

vec3 paint(vec2 screenSize, vec2 coords, float flow, float spin) {
    vec2 uv = (coords - 0.5 * screenSize) / length(screenSize) - vec2(P_OFFSET_X, P_OFFSET_Y);
    float radius = length(uv);
    float speed = spin + 302.2;
    float angle = atan(uv.y, uv.x) + speed - 20.0 * (P_SWIRL * radius + (1.0 - P_SWIRL));
    vec2 mid = (screenSize / length(screenSize)) / 2.0;
    uv = vec2(radius * cos(angle) + mid.x, radius * sin(angle) + mid.y) - mid;

    uv *= 30.0 / P_SCALE;
    speed = flow;
    vec2 uv2 = vec2(uv.x + uv.y);
    for (int i = 0; i < 8; i++) {
        if (i >= P_DETAIL) break;
        uv2 += sin(max(uv.x, uv.y)) + uv;
        uv += 0.5 * vec2(cos(5.1123314 + 0.353 * uv2.y + speed * 0.131121), sin(uv2.x - 0.113 * speed));
        uv -= cos(uv.x + uv.y) - sin(uv.x * 0.711 - uv.y);
    }

    float contrastMod = 0.25 * P_CONTRAST + 0.5 * P_SWIRL + 1.2;
    float amount = min(2.0, max(0.0, length(uv) * 0.035 * contrastMod));
    float c1 = max(0.0, 1.0 - contrastMod * abs(1.0 - amount));
    float c2 = max(0.0, 1.0 - contrastMod * abs(amount));
    float c3 = 1.0 - min(1.0, c1 + c2);
    float light = (P_LIGHTING - 0.2) * max(c1 * 5.0 - 4.0, 0.0) + P_LIGHTING * max(c2 * 5.0 - 4.0, 0.0);
#if P_LIGHT_MODE
    vec3 gap = mix(P_COLOR3, vec3(1.0), 0.9);
#else
    vec3 gap = P_COLOR3;
#endif
    float base = 0.3 / P_CONTRAST;
    return base * P_COLOR1 + (1.0 - base) * (P_COLOR1 * c1 + P_COLOR2 * c2 + gap * c3) + light;
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    // The component advances its flow at 7 units/s from 14 and the spin at
    // 0.2 * rotationSpeed rad/s, starting from -0.4.
    float t = wl_time();
    float flow = 14.0 + 7.0 * t;
    float spin = -0.4 + P_ROTATION_SPEED * 0.2 * t;

    float block = P_PIXEL_SIZE > 0.0 ? max(floor(P_PIXEL_SIZE + 0.5), 1.0) : 1.0;
    vec2 cell = floor(frag / block) + 0.5;
    vec3 color = paint(res, cell * block, flow, spin);
    float grain = (fract(52.9829189 * fract(dot(cell, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;
    color = clamp(color + grain, 0.0, 1.0);

    if (P_CRT > 0.0) {
        float glow = dot(color, vec3(0.2126, 0.7152, 0.0722));
        float period = max(block, 3.0);
        float row = fract((res.y - frag.y) / period);
        float line = wl_smoother((row - 0.5) / 0.42) * (1.0 - wl_smoother((row - 0.92) / 0.08));
        color *= 1.0 - P_CRT * 0.34 * line * (1.0 - 0.7 * glow);
        vec2 screen = frag / res;
        float edge = wl_smoother(screen.x / 0.18) * wl_smoother((1.0 - screen.x) / 0.18);
        edge *= wl_smoother(screen.y / 0.24) * wl_smoother((1.0 - screen.y) / 0.24);
        color *= mix(1.0, 0.78 + 0.22 * edge, P_CRT);
        color *= 1.0 + P_CRT * 0.1;
        color = min(color, vec3(1.0));
    }
    gl_FragColor = vec4(color, 1.0);
}
