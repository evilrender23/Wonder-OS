// RippleGrid — ported from React Bits <RippleGrid /> (see ../LICENSE-REACTBITS.md).
// The JS ripple list becomes analytic: idle ripples are born every
// «interval» seconds (centre or hashed random spots), so the shader rebuilds
// the live ones from the clock. Pointer wake, click ripples and intro dropped.

const int MAX_RIPPLES = 12;

float smootherstep_edge(float edge, float x) {
    float t = clamp(x / max(edge, 0.0001), 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec3 paper() {
    // Light mode needs a light page: keep the chosen background when it is
    // light, otherwise fall back to an off-white sheet.
    vec3 bg = P_BACKGROUND;
    return dot(bg, vec3(0.2126, 0.7152, 0.0722)) > 0.5 ? bg : vec3(0.961, 0.965, 0.973);
}

void main() {
    vec2 view = wl_res();
    vec2 css = wl_frag();
    float clock = wl_time();
    float tilt = radians(P_TILT);
    float focal = view.y * 1.15;

    vec2 screen = vec2(css.x - view.x * 0.5, view.y * 0.5 - css.y);
    vec2 plane = screen;
    float horizon = 1.0;
    if (tilt > 0.0001) {
        float depth = focal + screen.y * tan(tilt);
        horizon = smoothstep(0.0, focal * 0.35, depth);
        float scale = focal / max(depth, 0.001);
        plane = vec2(screen.x * scale, screen.y * scale / cos(tilt));
    }

    float waveLength = P_CELL_SIZE * 2.4 * P_RIPPLE_SIZE;
    float waveSpeed = 240.0 * P_RIPPLE_SPEED;
    float lifetime = 9.0 / max(0.2, P_RIPPLE_SPEED);
    float interval = P_INTERVAL;

    vec2 push = vec2(0.0);
    float lift = 0.0;
#if P_AUTO_RIPPLE != 2
    // Ripples spawn at clock 0.4 + k * interval; walk back from the newest.
    float since = clock - 0.4;
    float newest = floor(since / interval);
    for (int i = 0; i < MAX_RIPPLES; i++) {
        float k = newest - float(i);
        float age = since - k * interval;
        if (age > lifetime || k < 0.0) break;
        vec2 origin = vec2(0.0);
        float strength = 1.0;
#if P_AUTO_RIPPLE == 1
        // Random spot: x across the width, y in 20..90 % of the height.
        vec2 rnd = vec2(hash(vec2(k * 0.131, 7.31)), hash(vec2(3.17, k * 0.173)));
        vec2 spot = vec2((rnd.x - 0.5) * view.x, view.y * (0.2 + rnd.y * 0.7) - view.y * 0.5);
        if (tilt > 0.0001) {
            float depth = focal + spot.y * tan(tilt);
            if (depth <= focal * 0.05) continue;
            float scale = focal / depth;
            spot = vec2(spot.x * scale, spot.y * scale / cos(tilt));
        }
        origin = spot;
        strength = 0.75;
#endif
        float amount = strength * exp(-age * 0.55);
        vec2 delta = plane - origin;
        float dist = length(delta);
        float x = (dist - age * waveSpeed) / waveLength;
        float packet = exp(-x * x * 1.8);
        float spread = 1.0 / (1.0 + dist / (waveLength * 5.0));
        float calm = smoothstep(0.0, waveLength * 0.9, dist);
        float wave = sin(x * 6.28318) * packet * spread * calm * amount;
        push += (dist > 0.001 ? delta / dist : vec2(0.0)) * wave;
        lift += wave;
    }
#endif

    float angle = radians(P_ROTATION);
    vec2 turn = vec2(cos(angle), sin(angle));
    vec2 bent = plane + push * P_STRENGTH * P_CELL_SIZE * 0.22;
    vec2 grid = mat2(turn.x, turn.y, -turn.y, turn.x) * bent / P_CELL_SIZE;
    vec2 fw = max(fwidth(grid), vec2(0.00001));
    vec2 offset = abs(fract(grid - 0.5) - 0.5);
    vec2 dist = offset / fw;
    float width = max(P_LINE_WIDTH, 1.0);
    float thin = min(P_LINE_WIDTH, 1.0);
    float shape = 0.0;
    float edge = min(dist.x, dist.y);
#if P_VARIANT == 1
    edge = length(dist);
    shape = (1.0 - smoothstep(width * 1.7 - 0.5, width * 1.7 + 0.5, edge)) * thin * 1.8;
#elif P_VARIANT == 2
    float arm = 0.16;
    vec2 cover = (1.0 - smoothstep(vec2(width * 0.5 - 0.5), vec2(width * 0.5 + 0.5), dist)) * thin;
    vec2 reach = 1.0 - smoothstep(arm - fw * 0.5, arm + fw * 0.5, offset);
    shape = max(cover.x * reach.y, cover.y * reach.x);
    edge = max(min(dist.x, dist.y), length(max(offset - arm, 0.0) / fw));
#else
    vec2 cover = (1.0 - smoothstep(vec2(width * 0.5 - 0.5), vec2(width * 0.5 + 0.5), dist)) * thin;
    shape = max(cover.x, cover.y);
#endif
    float halo = exp(-edge / 2.4) * P_GLOW;
    float far = 1.0 - smoothstep(0.18, 0.5, max(fw.x, fw.y));

    float shine = max(1.0 + lift * 3.4 * P_STRENGTH, 0.25);

    vec2 uv = css / view;
    float edgeFade = P_FADE * 0.5;
    float mask = smootherstep_edge(edgeFade, uv.x) * smootherstep_edge(edgeFade, 1.0 - uv.x)
               * smootherstep_edge(edgeFade, uv.y) * smootherstep_edge(edgeFade, 1.0 - uv.y);
    mask *= horizon * far;

    float level = (shape * 0.26 + halo * 0.1) * shine * mask;
    float dither = (hash(css) - 0.5) / 255.0;
#if P_LIGHT_MODE
    vec3 tint = pow(P_COLOR, vec3(2.2));
    float tone = dot(tint, vec3(0.2126, 0.7152, 0.0722));
    float depthInk = min(1.0, 0.12 / max(tone, 0.001));
    vec3 ink = pow(min(vec3(1.0), tint * depthInk), vec3(1.0 / 2.2));
    float alpha = clamp(level * 1.2 + dither, 0.0, 1.0);
    gl_FragColor = vec4(wl_over(paper(), vec4(ink * alpha, alpha)), 1.0);
#else
    float alpha = clamp(level + dither, 0.0, 1.0);
    vec3 color = mix(P_COLOR, vec3(1.0), clamp(level - 0.7, 0.0, 1.0));
    gl_FragColor = vec4(wl_over(P_BACKGROUND, vec4(color * alpha, alpha)), 1.0);
#endif
}
