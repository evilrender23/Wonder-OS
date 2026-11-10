// Web Threads — ported from React Bits <WebThreads /> (see ../LICENSE-REACTBITS.md).
// Same single-pass field as the original; the cursor focus, click surges and
// the draw-in intro are dropped. Output is premultiplied, composited over
// the background colour.

const float PI = 3.14159265;
const float TAU = 6.28318530;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec3 stop_color(int i) {
    if (i <= 0) return P_COLOR1;
    if (i == 1) return P_COLOR2;
    if (i == 2) return P_COLOR3;
    return P_COLOR4;
}

vec3 palette(float t) {
    if (P_COLOR_COUNT <= 1) return P_COLOR1;
    float scaled = clamp(t, 0.0, 1.0) * float(P_COLOR_COUNT - 1);
    float fi = min(floor(scaled), float(P_COLOR_COUNT - 2));
    int index = int(fi);
    return mix(stop_color(index), stop_color(index + 1), scaled - fi);
}

float nodes() {
    return float(P_NODES);
}

vec2 envelope(float x) {
    float dx = x - P_PINCH;
    if (nodes() <= 1.0) return vec2(P_SPREAD * abs(dx), P_SPREAD * sign(dx));
    float k = PI * nodes();
    float c = cos(k * (dx + 0.5));
    return vec2(P_SPREAD * 0.5 * abs(c), -P_SPREAD * 0.5 * k * sin(k * (dx + 0.5)) * sign(c));
}

float hubAt(float x) {
    if (nodes() <= 1.0) return P_PINCH;
    float spacing = 1.0 / nodes();
    return P_PINCH - 0.5 + (floor((x - P_PINCH + 0.5) / spacing) + 0.5) * spacing;
}

vec3 thread(float x, float i, float n, float phase, float clock) {
    float lane = nodes() <= 1.0 ? x - P_PINCH : (x - P_PINCH + 0.5) * nodes() - 0.5;
#if P_MIRROR
    float side = mod(floor(lane), 2.0) < 0.5 ? -1.0 : 1.0;
#else
    float side = 1.0;
#endif
    vec2 env = envelope(x);
    float amp = 1.0 + i * P_TAPER;
#if P_SHIMMER
    float shimmer = sin(clock * 1.7 + i * 1.3) * 0.35;
#else
    float shimmer = 0.0;
#endif
    float angle = x * P_FREQUENCY + (phase + i * TAU / max(n, 1.0)) * side + shimmer;
    float wave = sin(angle);
    float y = P_POSITION - env.x * amp * wave;
    float slope = -(env.y * amp * wave + env.x * amp * cos(angle) * P_FREQUENCY);
    return vec3(y, slope, angle);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float clock = wl_time();
    // The weave phase runs at the component's default speed (0.2).
    float phase = clock * 0.2;

    vec2 p = frag / res;
    float aspect = res.x / res.y;
    float ang = radians(P_ANGLE);
    vec2 cs = vec2(cos(ang), sin(ang));
    vec2 q = (p - 0.5) * vec2(aspect, 1.0);
    q = vec2(q.x * cs.x + q.y * cs.y, -q.x * cs.y + q.y * cs.x);
    float x = q.x / aspect + 0.5;
    float y = q.y + 0.5;

    float n = float(P_THREAD_COUNT);
    float pixel = 1.0 / res.y;
    float hub = hubAt(x);
    float glowAmt = P_GLOW * sqrt(6.0 / max(n, 6.0));

    vec3 color = vec3(0.0);
    float energy = 0.0;
    float ink = 0.0;
    vec3 inkColor = vec3(0.0);

    for (int idx = 0; idx < 24; idx++) {
        if (idx >= P_THREAD_COUNT) break;
        float i = float(idx);
        float ci = n > 1.0 ? i / (n - 1.0) : 0.0;

        vec3 t = thread(x, i, n, phase, clock);
        float twist = P_DEPTH * smoothstep(0.0, 0.07, abs(x - hub));
        float depth = 0.5 + 0.5 * cos(t.z);
        float front = mix(1.0, 0.3 + 0.7 * depth, twist);
        float width = P_THICKNESS * mix(1.0, 0.55 + 0.7 * depth, twist);

        float bend = t.y * smoothstep(0.0, 0.04, abs(x - hub));
        float d = abs(y - t.x) / sqrt(1.0 + bend * bend / (aspect * aspect));
        float g = glowAmt / pow(max(d / width, pixel * 0.35), P_FALLOFF) * front;
        float onLine = exp(-d / (width * 0.012));

        if (P_PULSES > 0.0) {
            float travel = abs(x - hub);
            float pulse = pow(0.5 + 0.5 * sin(travel * 16.0 + clock * 2.2 + i * 1.9), 24.0);
            g *= 1.0 + P_PULSES * 5.0 * pulse * onLine;
        }

        vec3 tint = palette(ci);
        color += g * tint;
        energy += g;
        float stroke = smoothstep(0.45, 1.3, g);
        inkColor += tint * stroke;
        ink += stroke;
    }

    float fade = 1.0;
    if (P_FADE > 0.0) {
        float edge = min(x, 1.0 - x);
        float f = clamp(edge / max(P_FADE * 0.5, 0.001), 0.0, 1.0);
        fade = wl_smoother(f);
    }
    float core = smoothstep(0.5, 2.2, energy);
    color = mix(color, P_CORE_COLOR * energy, core * 0.5);
    color *= P_BRIGHTNESS * fade;
    float alpha = clamp(energy * fade, 0.0, 1.0);
    float grain = (hash(frag + fract(clock) * 97.0) - 0.5) * P_GRAIN;

#if P_LIGHT_MODE
    ink *= fade;
    vec3 hue = ink > 0.0001 ? inkColor / ink : palette(0.5);
    float body = clamp(ink, 0.0, 1.0);
    float haze = clamp(1.0 - exp(-energy * fade * 0.3), 0.0, 1.0) * 0.12;
    float focal = smoothstep(2.0, 9.0, energy * fade) * 0.25;
    float cover = clamp((body * 0.9 + (haze + focal) * (1.0 - body)) * (1.0 + grain * 2.0), 0.0, 0.95);
    gl_FragColor = vec4(clamp(mix(P_PAPER, hue, cover), 0.0, 1.0), 1.0);
#else
    vec3 rgb = max(color * alpha * (1.0 + grain * 2.0), 0.0);
    gl_FragColor = vec4(clamp(wl_over(P_BACKGROUND, vec4(rgb, alpha)), 0.0, 1.0), 1.0);
#endif
}
