// Orb — ported from React Bits <Orb /> (see ../LICENSE-REACTBITS.md).
// Single pass already; the pointer reach, click pulse/echo and the intro
// spring are dropped. «Siempre activo» reproduces forceHoverState.

const float FOCAL = 3.2;

vec3 hash33(vec3 p3) {
    p3 = fract(p3 * vec3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yxz + 19.19);
    return -1.0 + 2.0 * fract(vec3(p3.x + p3.y, p3.x + p3.z, p3.y + p3.z) * p3.zyx);
}

float snoise3(vec3 p) {
    const float K1 = 0.333333333;
    const float K2 = 0.166666667;
    vec3 i = floor(p + (p.x + p.y + p.z) * K1);
    vec3 d0 = p - (i - (i.x + i.y + i.z) * K2);
    vec3 e = step(vec3(0.0), d0 - d0.yzx);
    vec3 i1 = e * (1.0 - e.zxy);
    vec3 i2 = 1.0 - e.zxy * (1.0 - e);
    vec3 d1 = d0 - (i1 - K2);
    vec3 d2 = d0 - (i2 - K1);
    vec3 d3 = d0 - 0.5;
    vec4 h = max(0.6 - vec4(dot(d0, d0), dot(d1, d1), dot(d2, d2), dot(d3, d3)), 0.0);
    vec4 n = h * h * h * h * vec4(dot(d0, hash33(i)), dot(d1, hash33(i + i1)),
                                  dot(d2, hash33(i + i2)), dot(d3, hash33(i + 1.0)));
    return dot(vec4(31.316), n);
}

float light1(float intensity, float attenuation, float dist) {
    return intensity / (1.0 + dist * attenuation);
}

float light2(float intensity, float attenuation, float dist) {
    return intensity / (1.0 + dist * dist * attenuation);
}

vec3 shift_hue(vec3 rgb) {
    float angle = radians(P_HUE);
    float c = cos(angle);
    float s = sin(angle);
    float y = dot(rgb, vec3(0.299, 0.587, 0.114));
    float i = dot(rgb, vec3(0.596, -0.274, -0.322));
    float q = dot(rgb, vec3(0.211, -0.523, 0.312));
    float ni = i * c - q * s;
    float nq = i * s + q * c;
    return vec3(y + 0.956 * ni + 0.621 * nq, y - 0.272 * ni - 0.647 * nq, y - 1.106 * ni + 1.703 * nq);
}

vec4 draw(vec2 uv, float time, float swirl, float orbit, float awake,
          vec3 c1, vec3 c2, vec3 c3) {
    float len = length(uv);
    vec2 dir = len > 0.0001 ? uv / len : vec2(1.0, 0.0);
    float ang = atan(dir.y, dir.x);
    float invLen = len > 0.0 ? 1.0 / len : 0.0;
    vec3 bg = P_BACKGROUND;
    float bgLuminance = dot(bg, vec3(0.299, 0.587, 0.114));

    float n0 = snoise3(vec3(uv * 0.65, time * 0.5)) * 0.5 + 0.5;
    float r0 = mix(0.8 - 0.04 * P_WOBBLE, 0.8 + 0.04 * P_WOBBLE, n0);
    float d0 = distance(uv, (r0 * invLen) * uv);
    float v0 = light1(1.0, 10.0 / P_GLOW, d0);
    v0 *= smoothstep(r0 * 1.05, r0, len);
    float reach = max(P_BLEED, 0.04);
    float innerFade = smoothstep(r0 * (1.0 - 0.2 * reach), r0 * (1.0 - 0.05 * reach), len);
    v0 *= mix(innerFade, 1.0, bgLuminance * 0.7 * min(P_BLEED, 1.0));
    float body = smoothstep(0.8 - 0.2 * reach, 0.8, len);
    float cl = cos(ang + swirl) * 0.5 + 0.5;

    vec2 head = vec2(cos(orbit), sin(orbit));
    vec2 pos = head * r0;
    float d = distance(uv, pos);
    float lag = atan(head.x * dir.y - head.y * dir.x, dot(dir, head));
    float trail = P_TRAIL > 0.0 ? smoothstep(0.0, 0.35, lag) * exp(-max(lag, 0.0) * 2.6 / max(P_TRAIL, 0.001)) : 0.0;
    float v1 = (light2(1.5, 5.0, d) + trail * 0.6) * P_HIGHLIGHT;
    v1 *= light1(1.0, 50.0, d0);
    float band = v0 * smoothstep(1.0, mix(0.6, 1.0, n0 * 0.5), len) * body;
    float heat = pow(clamp(band / 0.82, 0.0, 1.0), 3.0);
    float bloom = light2(1.0, 160.0 / P_GLOW, max(len - r0, 0.0)) * smoothstep(0.4, 0.0, len - r0) * step(r0, len);

    float v2 = smoothstep(1.0, mix(0.6, 1.0, n0 * 0.5), len);
    float v3 = body;

    vec3 colBase = mix(c1, c2, cl);
    float fadeAmount = mix(1.0, 0.1, bgLuminance);

    vec3 darkCol = mix(c3, colBase, v0);
    darkCol = (darkCol + v1) * v2 * v3 * (1.0 + awake) + mix(colBase, vec3(1.0), 0.25) * heat * 0.28;
    darkCol = clamp(darkCol + colBase * (bloom * 0.1), 0.0, 1.0);

    vec3 lightCol = (colBase + v1) * mix(1.0, v2 * v3, fadeAmount);
    lightCol = mix(bg, lightCol, clamp(v0, 0.0, 1.0));
    lightCol = mix(lightCol, colBase, clamp(v0, 0.0, 1.0) * clamp(awake, 0.0, 0.6));
    lightCol = clamp(mix(lightCol, vec3(1.0), clamp(v1 * 0.35, 0.0, 0.6)), 0.0, 1.0);

    vec3 finalCol = mix(darkCol, lightCol, bgLuminance);
    float alpha = max(max(finalCol.r, finalCol.g), finalCol.b);
    return vec4(finalCol / (alpha + 1e-5), alpha);
}

vec2 to_plane(vec2 p, out float hit) {
    float ax = radians(-P_TILT_X);
    float ay = radians(P_TILT_Y);
    float cx = cos(ax);
    float sx = sin(ax);
    float cy = cos(ay);
    float sy = sin(ay);
    vec3 ex = vec3(cy, 0.0, -sy);
    vec3 ey = vec3(sy * sx, cx, cy * sx);
    vec3 normal = vec3(sy * cx, -sx, cy * cx);
    vec3 origin = vec3(0.0, 0.0, FOCAL);
    vec3 ray = vec3(p, -FOCAL);
    float denom = dot(normal, ray);
    float t = -dot(normal, origin) / (abs(denom) > 0.00001 ? denom : -0.00001);
    hit = t > 0.0 ? 1.0 : 0.0;
    vec3 q = origin + ray * t;
    return vec2(dot(q, ex), dot(q, ey));
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float t = wl_time();

    // No pointer: the swirl turns at 2x, the light orbits clockwise.
    float swirl = 2.0 * t;
    float orbit = -t;
    float hover = P_FORCE_HOVER == 1 ? 1.0 : 0.0;
    float awake = hover * P_HOVER_INTENSITY * 0.18;
    float rotation = hover * 0.3 * t;

    vec2 uv = (frag - res * 0.5) / min(res.x, res.y) * 2.0;
    uv /= P_SIZE;
    float hit = 1.0;
    if (abs(P_TILT_X) + abs(P_TILT_Y) > 0.0001) uv = to_plane(uv, hit);

    float s = sin(rotation);
    float c = cos(rotation);
    uv = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);

    vec3 c1 = clamp(shift_hue(P_COLOR_1), 0.0, 1.0);
    vec3 c2 = clamp(shift_hue(P_COLOR_2), 0.0, 1.0);
    vec3 c3 = clamp(shift_hue(P_COLOR_3), 0.0, 1.0);
    vec4 col = draw(uv, t, swirl, orbit, awake, c1, c2, c3);
    float dither = (fract(sin(dot(frag, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
    float alpha = clamp(col.a * hit + dither, 0.0, 1.0);
    gl_FragColor = vec4(wl_over(P_BACKGROUND, vec4(col.rgb * alpha * alpha, alpha)), 1.0);
}
