// LightPillar — ported from React Bits <LightPillar /> (see ../LICENSE-REACTBITS.md).
// The volumetric march runs at full resolution with 30 of the 80 steps (each
// step 2.7x as long, weighted to keep the same light per unit length); the
// mip-chain bloom becomes an analytic halo around the pillar axis. Cursor
// lean/drift, click pulses and the intro are dropped.

#ifndef LP_STEPS
#define LP_STEPS 30
#endif
#define LP_STRIDE (80.0 / float(LP_STEPS))

float smootherstep01(float t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

vec3 tanh3(vec3 x) {
    vec3 e = exp(2.0 * clamp(x, -10.0, 10.0));
    return (e - 1.0) / (e + 1.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    vec2 vUv = frag / res;
    // clock advances at 0.96 * rotationSpeed (default 0.3).
    float clock = wl_time() * 0.96 * 0.3;
    float utime = clock * P_FLOW;
    float angle = clock * 0.3;
    float tilt = P_PILLAR_ROTATION * 0.017453292;
    vec2 prot = vec2(cos(tilt), sin(tilt));
    vec2 rot = vec2(cos(angle), sin(angle));
    vec2 wave = vec2(sin(P_TWIST), cos(P_TWIST));
    float width = P_PILLAR_WIDTH;
    float width_norm = width / 3.0;

    vec2 uv = (vUv * 2.0 - 1.0) * vec2(res.x / res.y, 1.0);
    uv = vec2(prot.x * uv.x - prot.y * uv.y, prot.y * uv.x + prot.x * uv.y);

    vec3 ro = vec3(0.0, 0.0, -10.0);
    vec3 rd = normalize(vec3(uv, 1.0));
    vec3 origin = vec3(rot.x * ro.x - rot.y * ro.z, ro.y, rot.y * ro.x + rot.x * ro.z);
    vec3 dir = vec3(rot.x * rd.x - rot.y * rd.z, rd.y, rot.y * rd.x + rot.x * rd.z);

    float glow = 0.0;
    float lift = 0.0;
    float t = 0.1;
    for (int i = 0; i < LP_STEPS; i++) {
        vec3 p = origin + dir * t;
        vec3 q = p;
        q.y = p.y * P_PILLAR_HEIGHT + utime;
        float freq = 1.0;
        float amp = 1.0;
        for (int j = 0; j < 6; j++) {
            if (j >= P_DETAIL) break;
            q.xz = vec2(wave.y * q.x - wave.x * q.z, wave.x * q.x + wave.y * q.z);
            q += cos(q.zxy * freq - utime * float(j) * 2.0) * amp;
            freq *= 2.0;
            amp *= 0.5;
        }
        float d = length(cos(q.xz)) - 0.2;
        float bound = length(p.xz) - width;
        float h = max(4.0 - abs(d - bound), 0.0);
        d = max(d, bound) + h * h * 0.015625;
        d = abs(d) * 0.15 + 0.01;
        float weight = LP_STRIDE / d;
        glow += weight;
        lift += clamp((15.0 - p.y) / 30.0, 0.0, 1.0) * weight;
        t += d * LP_STRIDE;
        if (t > 50.0) break;
    }

    vec3 col = tanh3((P_BOTTOM_COLOR * glow + (P_TOP_COLOR - P_BOTTOM_COLOR) * lift) * P_GLOW_AMOUNT / width_norm);

    // Bloom: soft halo hugging the pillar, tinted by the average colour.
    vec2 p2 = (vUv * 2.0 - 1.0) * vec2(res.x / res.y, 1.0);
    float axis = prot.y * p2.x + prot.x * p2.y;
    float across = prot.x * p2.x - prot.y * p2.y;
    if (P_BLOOM > 0.0) {
        float reach = 0.1 * width + 0.12;
        float core = exp(-across * across / (reach * reach)) * 0.55
                   + exp(-across * across / (reach * reach * 4.0)) * 0.45;
        float peak = max(col.r, max(col.g, col.b));
        float halo = core * (0.35 + 0.65 * smoothstep(0.2, 0.9, peak));
        vec3 tint = mix(P_BOTTOM_COLOR, P_TOP_COLOR, clamp(0.5 - axis * 0.35, 0.0, 1.0)) + col;
        tint /= max(max(tint.r, max(tint.g, tint.b)), 0.001);
        vec3 g = clamp(tint * halo * P_BLOOM * 0.55 * min(P_GLOW_AMOUNT / 0.002, 2.0), 0.0, 1.0);
        col = 1.0 - (1.0 - clamp(col, 0.0, 1.0)) * (1.0 - g);
    }

    col -= fract(sin(dot(frag, vec2(12.9898, 78.233))) * 43758.5453) / 15.0 * P_NOISE_INTENSITY;
    vec3 result = clamp(col * P_INTENSITY, 0.0, 1.0);

    float mask = 1.0;
    float edge = P_FADE * 0.5;
    if (edge > 0.0) {
        mask *= smootherstep01(vUv.x / edge) * smootherstep01((1.0 - vUv.x) / edge);
        mask *= smootherstep01(vUv.y / edge) * smootherstep01((1.0 - vUv.y) / edge);
    }

#if P_LIGHT_MODE
    float vt = max(P_TOP_COLOR.r, max(P_TOP_COLOR.g, P_TOP_COLOR.b)) - min(P_TOP_COLOR.r, min(P_TOP_COLOR.g, P_TOP_COLOR.b));
    float vb = max(P_BOTTOM_COLOR.r, max(P_BOTTOM_COLOR.g, P_BOTTOM_COLOR.b)) - min(P_BOTTOM_COLOR.r, min(P_BOTTOM_COLOR.g, P_BOTTOM_COLOR.b));
    float neutral = 1.0 - clamp((max(vt, vb) - 0.08) / 0.12, 0.0, 1.0);
    float energy = max(result.r, max(result.g, result.b));
    vec3 hue = result / max(energy, 0.001);
    float coverage = smoothstep(0.025, 0.95, energy);
    hue = pow(clamp(hue, 0.0, 1.0), vec3(1.25));
    vec3 tinted = mix(vec3(1.0), hue, coverage * 0.94 * mask);
    vec3 ink = vec3(1.0) - pow(result, vec3(1.6)) * 0.85 * mask;
    result = mix(tinted, ink, neutral);
#else
    result *= mask;
    // mix-blend-mode: screen over the page.
    result = 1.0 - (1.0 - result) * (1.0 - P_BACKGROUND);
#endif
    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
