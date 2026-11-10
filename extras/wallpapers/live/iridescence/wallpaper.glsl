// Iridescence — ported from React Bits <Iridescence /> (see ../LICENSE-REACTBITS.md).
// Single pass already; the pointer parallax, the stir field texture, the
// sheen spot and the click swirls are dropped (no interaction).

vec3 hue_rotate(vec3 c, float angle) {
    vec3 k = vec3(0.57735027);
    float ca = cos(angle);
    return c * ca + cross(k, c) * sin(angle) + k * dot(k, c) * (1.0 - ca);
}

float smootherstep_edge(float edge, float x) {
    float t = clamp(x / edge, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float grain_hash(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    vec2 vUv = frag / res;
    float mr = min(res.x, res.y);
    vec2 aspect = res / mr;
    // phase += dt * speed (default 1).
    float time = wl_time();

    vec2 uv = (vUv * 2.0 - 1.0) * aspect;
    float rot = radians(P_ROTATION);
    float c = cos(rot);
    float s = sin(rot);
    uv = mat2(c, -s, s, c) * uv / P_SCALE;

    float d = -time * 0.5;
    float a = 0.0;
    for (int i = 0; i < 24; i++) {
        if (i >= P_DETAIL) break;
        float fi = float(i);
        a += cos(fi - d - a * uv.x * P_WARP);
        d += sin(uv.y * fi + a);
    }
    d += time * 0.5;
    vec3 col = vec3(cos(uv * vec2(d, a)) * 0.6 + 0.4, cos(a + d) * 0.5 + 0.5);
    col = cos(col * cos(vec3(d, a, 2.5)) * 0.5 + 0.5) * P_COLOR;

    col = hue_rotate(col, radians(P_HUE_SHIFT));
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(luma), col, P_SATURATION);
    col = (col - 0.5) * P_CONTRAST + 0.5;
    col *= P_BRIGHTNESS;
    if (P_GRAIN > 0.0) {
        float seed = mod(floor(time * 1000.0 / 42.0), 997.0);
        col += (grain_hash(frag + seed) - 0.5) * P_GRAIN * 0.12;
    }
    col = clamp(col, 0.0, 1.0);

    float alpha = 1.0;
    if (P_FADE > 0.0) {
        float edge = P_FADE * mr * 0.5;
        alpha *= smootherstep_edge(edge, frag.x) * smootherstep_edge(edge, res.x - frag.x);
        alpha *= smootherstep_edge(edge, frag.y) * smootherstep_edge(edge, res.y - frag.y);
    }
    gl_FragColor = vec4(wl_over(P_BACKGROUND, vec4(col * alpha, alpha)), 1.0);
}
