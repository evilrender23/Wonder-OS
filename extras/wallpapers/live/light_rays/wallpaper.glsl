// LightRays — ported from React Bits <LightRays /> (see ../LICENSE-REACTBITS.md).
// Already single pass; the beam no longer turns toward the cursor, and the
// cursor shadow, click pulses and intro are dropped.

float lr_hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(mix(lr_hash(i), lr_hash(i + vec2(1.0, 0.0)), w.x),
               mix(lr_hash(i + vec2(0.0, 1.0)), lr_hash(i + vec2(1.0, 1.0)), w.x), w.y);
}

float shape(float strength, float width) {
    float edge = max(mix(0.5, 0.08, P_SHARPNESS), width);
    return mix(strength, smoothstep(0.62 - edge, 0.62 + edge, strength), P_SHARPNESS);
}

// Anchor (fractions of the container, y down) and base direction per origin.
void origin(out vec2 anchor, out vec2 dir) {
    int o = P_ORIGIN;
    anchor = vec2(0.5, -0.2);
    dir = vec2(0.0, 1.0);
    if (o == 1) anchor = vec2(0.0, -0.2);
    if (o == 2) anchor = vec2(1.0, -0.2);
    if (o == 3) { anchor = vec2(-0.2, 0.5); dir = vec2(1.0, 0.0); }
    if (o == 4) { anchor = vec2(1.2, 0.5); dir = vec2(-1.0, 0.0); }
    if (o == 5) { anchor = vec2(0.5, 1.2); dir = vec2(0.0, -1.0); }
    if (o == 6) { anchor = vec2(0.0, 1.2); dir = vec2(0.0, -1.0); }
    if (o == 7) { anchor = vec2(1.0, 1.2); dir = vec2(0.0, -1.0); }
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float time = wl_time();
    vec2 coord = vec2(frag.x, res.y - frag.y);

    vec2 anchor;
    vec2 base_dir;
    origin(anchor, base_dir);
    vec2 source = anchor * res;
    float tilt = radians(P_TILT);
    vec2 aim = vec2(base_dir.x * cos(tilt) - base_dir.y * sin(tilt),
                    base_dir.x * sin(tilt) + base_dir.y * cos(tilt));
    float reach = max(res.x, res.y);

    vec2 delta = coord - source;
    float dist = length(delta);
    vec2 dir = delta / max(dist, 1e-4);
    float cos_angle = dot(dir, aim);
    float angle = cos_angle + P_DISTORTION * sin(time * 2.0 + dist * 0.02) * 0.2;
    float slope = fwidth(angle);
    float depth = dist / reach;

    float src = exp(-depth * depth * 9.0) * 0.9 + exp(-depth * 3.2) * 0.25;
    float glow_light = P_GLOW * src * smoothstep(-0.2, 0.6, cos_angle);
    float spread = pow(max(angle, 0.0), 1.0 / P_SPREAD);
    float length_falloff = clamp(1.0 - dist / (reach * P_RAY_LENGTH), 0.0, 1.0);
    float fade_falloff = clamp(1.0 - dist / (reach * P_FADE_DISTANCE), 0.5, 1.0);
    float envelope = spread * length_falloff * fade_falloff;
    float reveal = P_INTENSITY * (1.0 - smoothstep(2.25, 2.6, depth));

    float light = 0.0;
    if ((envelope * 1.6 + glow_light) * reveal >= 0.0015) {
        float phase_a = angle * 36.2214 * P_DENSITY + time * 1.5;
        float phase_b = -angle * 21.11349 * P_DENSITY + time * 1.5;
        float phase_c = angle * 22.3991 * P_DENSITY + time * 1.1;
        float phase_d = -angle * 18.0234 * P_DENSITY + time * 1.1;
        float strength_a = clamp(0.75 + 0.15 * sin(phase_a) + 0.2 * cos(phase_b), 0.0, 1.0);
        float strength_b = clamp(0.75 + 0.15 * sin(phase_c) + 0.2 * cos(phase_d), 0.0, 1.0);
        float width_a = abs(0.15 * 36.2214 * cos(phase_a) + 0.2 * 21.11349 * sin(phase_b)) * P_DENSITY * slope;
        float width_b = abs(0.15 * 22.3991 * cos(phase_c) + 0.2 * 18.0234 * sin(phase_d)) * P_DENSITY * slope;
#if P_PULSATING
        float pulse_a = 0.8 + 0.2 * sin(time * 4.5);
        float pulse_b = 0.8 + 0.2 * sin(time * 3.3);
#else
        float pulse_a = 1.0;
        float pulse_b = 1.0;
#endif
        float ray_light = envelope * (0.5 * shape(strength_a, width_a) * pulse_a + 0.4 * shape(strength_b, width_b) * pulse_b);

        if (P_HAZE > 0.0) {
            float around = atan(aim.x * dir.y - aim.y * dir.x, cos_angle);
            vec2 drift = vec2(around * 2.2 + time * 0.035, depth * 2.6 - time * 0.11);
            float fog = 0.65 * value_noise(drift * vec2(3.0, 1.0)) + 0.35 * value_noise(drift * vec2(7.0, 2.3) + 4.1);
            ray_light *= mix(1.0, 0.25 + 1.2 * fog, P_HAZE);
        }
        light = (ray_light + glow_light) * reveal;

        if (P_NOISE > 0.0) {
            float n = fract(sin(dot(coord * 0.02 + time * 0.1, vec2(12.9898, 78.233))) * 43758.5453123);
            light *= 1.0 - P_NOISE + P_NOISE * n;
        }
    }

    float near = clamp(1.2 - dist / res.y, 0.0, 1.0);
    vec3 tint = mix(vec3(0.9), vec3(0.1, 0.3, 0.5) + near * vec3(0.8, 0.6, 0.5), P_ATMOSPHERE);
    float gray = dot(tint, vec3(0.299, 0.587, 0.114));
    vec3 hue = max(mix(vec3(gray), tint, P_SATURATION), 0.0) * P_COLOR;

    float grain = (fract(52.9829189 * fract(dot(floor(frag) + 0.5, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;
#if P_LIGHT_MODE
    vec3 color = hue * light;
    float energy = max(color.r, max(color.g, color.b));
    vec3 tone = clamp(1.0 - (1.0 - hue / max(max(hue.r, max(hue.g, hue.b)), 1e-4)) * 1.8, 0.0, 1.0);
    float warm = clamp((min(tone.r, tone.g) - tone.b) * 2.0, 0.0, 1.0);
    vec3 ink = tone * (mix(0.66, 0.94, warm) - 0.1 * smoothstep(0.4, 1.2, energy));
    float density = clamp(0.82 * (1.0 - exp(-1.5 * energy * energy)) + grain, 0.0, 1.0);
    // Light mode prints on a light page: the ink sits over the inverted
    // background so a dark setting still gives paper.
    vec3 page = vec3(1.0) - P_BACKGROUND * 0.35;
    gl_FragColor = vec4(wl_over(page, vec4(ink * density, density)), 1.0);
#else
    float level = light < 0.75 ? light : 0.75 + 0.25 * (1.0 - exp(-(light - 0.75) / 0.25));
    float top = max(hue.r, max(hue.g, hue.b));
    hue = mix(hue, vec3(top), smoothstep(0.8, 1.0, level) * 0.45);
    vec3 shown = clamp(hue * level, 0.0, 1.0) * level;
    vec4 premul = vec4(clamp(shown + grain, 0.0, level), level);
    gl_FragColor = vec4(wl_over(P_BACKGROUND, premul), 1.0);
#endif
}
