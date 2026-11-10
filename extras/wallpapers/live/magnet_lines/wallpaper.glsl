// MagnetLines — ported from React Bits <MagnetLines /> (see ../LICENSE-REACTBITS.md).
// The instanced quads become a per-pixel loop over the neighbouring grid
// cells; each line's angle is evaluated analytically from the field. No
// pointer: the field either rests on the centre or follows the component's
// own idle wander path. The spring (inertia/bounce), click pulses and the
// intro are dropped; the ripple delay is kept as a per-line time lag.

const float PI = 3.14159265;
const float TAU = 6.28318531;
const float FAR = 4.0;

float wrap_angle(float value, float period) {
    float half_p = period * 0.5;
    return mod(value + half_p, period) - half_p;
}

float smootherstep01(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float ndot(vec2 a, vec2 b) {
    return a.x * b.x - a.y * b.y;
}

float rhombus(vec2 p, vec2 b) {
    p = abs(p);
    float h = clamp(ndot(b - 2.0 * p, b) / dot(b, b), -1.0, 1.0);
    float d = length(p - 0.5 * b * vec2(1.0 - h, 1.0 + h));
    return d * sign(p.x * b.y + p.y * b.x - b.x * b.y);
}

float triangle_sd(vec2 p, vec2 q) {
    p.x = abs(p.x);
    vec2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0, 1.0);
    vec2 b = p - q * vec2(clamp(p.x / q.x, 0.0, 1.0), 1.0);
    float s = -sign(q.y);
    vec2 d = min(vec2(dot(a, a), s * (p.x * q.y - p.y * q.x)), vec2(dot(b, b), s * (p.y - q.y)));
    return -sqrt(d.x) * sign(d.y);
}

float capsule(vec2 p, float from, float to, float radius) {
    float x = clamp(p.x, from, to);
    return length(vec2(p.x - x, p.y)) - radius;
}

float feather(float distance, float zone) {
    if (zone <= 0.0) return 1.0;
    return smootherstep01(clamp(distance / zone, 0.0, 1.0));
}

vec3 spectrum(float hue, float tone) {
    vec3 k = clamp(abs(fract(hue + vec3(0.0, 2.0, 1.0) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    k = k * k * (3.0 - 2.0 * k);
    float chroma = (1.0 - abs(2.0 * tone - 1.0)) * 0.92;
    return tone + chroma * (k - 0.5);
}

// Tilt homography (the component's viewFor, fill mode), y-down pixels.
vec3 tilt_inverse(vec2 s, vec2 size) {
    float ax = radians(P_TILT_X);
    float ay = radians(P_TILT_Y);
    float depth = max(size.x, size.y) * 1.25;
    vec2 q = s - size * 0.5;
    float u0 = q.x / cos(ay);
    float u1 = (q.y - sin(ax) * sin(ay) * u0) / cos(ax);
    float u2 = 1.0 - (sin(ay) * cos(ax) / depth) * u0 + (sin(ax) / depth) * u1;
    return vec3(u0, u1, u2);
}

float depth_at(vec2 c, vec2 size) {
    float ax = radians(P_TILT_X);
    float ay = radians(P_TILT_Y);
    float depth = max(size.x, size.y) * 1.25;
    return (sin(ay) * cos(ax) / depth) * c.x - (sin(ax) / depth) * c.y + 1.0;
}

// The component's idle wander (centre-relative, y-down).
vec2 wander(float clock, float amount) {
#if P_IDLE
    return vec2(cos(clock * 0.37) * amount + sin(clock * 0.21) * amount * 0.35,
                sin(clock * 0.53 + 1.3) * amount * 0.8);
#else
    return vec2(0.0);
#endif
}

vec2 wander_velocity(float clock, float amount) {
    return vec2(-0.37 * sin(clock * 0.37) * amount + 0.21 * cos(clock * 0.21) * amount * 0.35,
                0.53 * cos(clock * 0.53 + 1.3) * amount * 0.8);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float clock = wl_time();
    vec2 S = vec2(frag.x, res.y - frag.y);

    bool tilted = abs(P_TILT_X) + abs(P_TILT_Y) > 0.0;
    vec2 p;
    float aa = 1.0;
    if (tilted) {
        vec3 u = tilt_inverse(S, res);
        if (u.z <= 0.0) {
            gl_FragColor = vec4(P_BACKGROUND, 1.0);
            return;
        }
        p = u.xy / u.z;
        vec3 ux = tilt_inverse(S + vec2(1.0, 0.0), res);
        vec3 uy = tilt_inverse(S + vec2(0.0, 1.0), res);
        vec2 dx = ux.xy / ux.z - p;
        vec2 dy = uy.xy / uy.z - p;
        aa = max(length(abs(dx) + abs(dy)) * 0.7071, 0.0001);
    } else {
        p = S - res * 0.5;
    }

    float period = P_SHAPE == 0 ? PI : TAU;
    float head = P_SHAPE == 2 ? 0.16 : (P_SHAPE == 1 ? 0.32 : 0.0);
    vec3 color = P_LINE_COLOR;
    vec3 accent = P_SHAPE == 2 ? P_ACCENT_COLOR : (P_USE_ACCENT == 1 ? P_ACCENT_COLOR : color);
    float accentMix = P_USE_ACCENT == 1 ? 1.0 : 0.0;
    float tone = clamp((max(color.r, max(color.g, color.b)) + min(color.r, min(color.g, color.b))) * 0.5, 0.42, 0.66);

    float columns = max(1.0, floor(res.x / P_SPACING + 0.5));
    float rows = max(1.0, floor(res.y / P_SPACING + 0.5));
    vec2 cellSize = res / vec2(columns, rows);
    float lineHalf = P_LINE_HEIGHT * 0.5;
    float widthHalf = P_LINE_WIDTH * 0.5;
    float reachMax = lineHalf * (1.0 + P_STRETCH) + max(widthHalf, head * lineHalf * (1.0 + P_STRETCH)) * 2.0 + aa * 2.0;
    vec2 span = ceil(reachMax / cellSize - 0.5);

    vec2 cellF = floor((p + res * 0.5) / cellSize);
    float amount = min(res.x, res.y) * 0.32;
    float lagBase = 0.6;
    float lag = P_RIPPLE / 900.0;
    vec2 target = wander(clock - lagBase, amount);
    vec2 vel = wander_velocity(clock - lagBase - 0.25, amount);
    float heading = atan(vel.y, vel.x);
    float spread = P_SCATTER * PI * 0.5;
    float baseAngle = radians(P_BASE_ANGLE);

    vec4 acc = vec4(0.0);
    for (int j = -2; j <= 2; j++) {
        if (abs(float(j)) > span.y) continue;
        for (int i = -2; i <= 2; i++) {
            if (abs(float(i)) > span.x) continue;
            vec2 cell = cellF + vec2(float(i), float(j));
            vec2 c = (cell + 0.5) * cellSize - res * 0.5;
            float shown = 1.0;
            if (tilted) {
                float w = depth_at(c, res);
                if (w <= 0.0 || w > FAR) continue;
                shown = 1.0 - smoothstep(FAR * 0.45, FAR, w);
            }

            // Lines further from the field source see it later (ripple).
            float delay = lag * length(target - c);
            vec2 seen = wander(clock - lagBase - delay, amount);
            vec2 dd = seen - c;
            float near = length(dd);
            float goal;
#if P_MODE == 3
            float dist = max(near, 1.0);
            vec2 rr = -dd / dist;
            vec2 mm = vec2(cos(heading), sin(heading));
            float dt = dot(mm, rr);
            goal = atan(3.0 * dt * rr.y - mm.y, 3.0 * dt * rr.x - mm.x);
#else
            float toward = atan(dd.y, dd.x);
            goal = toward + (P_MODE == 1 ? 0.0 : (P_MODE == 2 ? PI * 0.25 : PI * 0.5));
#endif
            float seed = wl_hash12(cell * vec2(1.0, 1.0) + vec2(40503.0, 9973.0)) * 2.0 - 1.0;
            float rest = baseAngle + PI * 0.5 + seed * spread;
            if (P_REACH > 0.0) {
                float k = clamp((P_REACH - near) / max(1.0, P_REACH * 0.45), 0.0, 1.0);
                goal = rest + wrap_angle(goal - rest, period) * smootherstep01(k);
            }
            float angle = goal;
            float emphasis = exp(-(near * near) / (P_RADIUS * P_RADIUS)) * shown;

            float halfLength0 = lineHalf * (1.0 + P_STRETCH * emphasis);
            vec2 axis = vec2(cos(angle), sin(angle));
            vec2 d = p - c;
            vec2 lp = vec2(dot(d, axis), dot(d, vec2(-axis.y, axis.x)));

            float halfWidth = max(widthHalf, aa * 0.5);
            float thin = widthHalf / halfWidth;
            float halfLength = max(halfLength0, halfWidth);
            float sd;
#if P_SHAPE == 1
            float headLen = min(halfLength * 0.9, max(halfWidth * 4.5, halfLength * 0.55));
            float wing = max(head * halfLength, halfWidth * 2.2);
            float shaft = capsule(lp, -halfLength + halfWidth, halfLength - headLen * 0.6, halfWidth);
            float tip = triangle_sd(vec2(lp.y, halfLength - lp.x), vec2(wing, headLen));
            sd = min(shaft, tip);
#elif P_SHAPE == 2
            sd = rhombus(lp, vec2(halfLength, max(head * halfLength, halfWidth)));
#else
            sd = capsule(lp, -halfLength + halfWidth, halfLength - halfWidth, halfWidth);
#endif
            float coverage = clamp(0.5 - sd / aa, 0.0, 1.0) * thin;
            if (coverage <= 0.0) continue;

            vec3 col = mix(color, accent, accentMix * clamp(emphasis, 0.0, 1.0));
#if P_SHAPE == 2
            float north = smoothstep(-aa, aa, lp.x);
            col = mix(color, accent, north);
#endif
#if P_RAINBOW
            vec3 tint = spectrum(fract(angle / period), tone);
#if P_SHAPE == 2
            col = mix(tint, color, 1.0 - smoothstep(-aa, aa, lp.x));
#else
            col = tint;
#endif
#endif
            float alpha = mix(1.0 - P_FADE, 1.0, clamp(emphasis, 0.0, 1.0));
            alpha = clamp(alpha, 0.0, 1.0) * coverage * shown;
            acc = vec4(col * alpha, alpha) + acc * (1.0 - alpha);
        }
    }

    vec2 inside = min(S, res - S);
    float edge = feather(inside.x, P_EDGE_FADE * 0.34 * res.x) * feather(inside.y, P_EDGE_FADE * 0.44 * res.y);
    acc *= edge;
    gl_FragColor = vec4(wl_over(P_BACKGROUND, acc), 1.0);
}
