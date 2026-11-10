// Lightfall — ported from React Bits <Lightfall /> (see ../LICENSE-REACTBITS.md).
// Single pass: the streak tunnel is evaluated exactly as in the original;
// the 4-level bloom mip chain becomes a wider analytic halo around each
// streak head and tail. Cursor lens, cursor light, hold-to-warp and the
// warp-speed intro are dropped.

const vec3 CAMERA = vec3(0.0, -0.8888889, -0.2222222);
const float TAU = 6.28318530718;
const float SPACING = 0.005;

float stroke(float d, float w) {
    float s = max(0.75 * w, 0.55);
    return 0.75 * w / s * exp(-0.5 * d * d / (s * s));
}

float orb(float d, float r) {
    float s = max(0.6 * r, 0.7);
    float e = 0.6 * r / s;
    return e * e * exp(-0.5 * d * d / (s * s));
}

vec3 lin(vec3 c) {
    return c * c;
}

vec3 streak_color(float hue) {
    int idx = int(min(floor(hue * float(P_COLOR_COUNT)), float(P_COLOR_COUNT - 1)));
    if (idx <= 0) return lin(P_COLOR_1);
    if (idx == 1) return lin(P_COLOR_2);
    if (idx == 2) return lin(P_COLOR_3);
    return lin(P_COLOR_4);
}

float smootherstep01(float t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

void main() {
    vec2 view = wl_res();
    vec2 frag = wl_frag();
#if P_LIGHT_MODE
    float lightm = 1.0;
#else
    float lightm = 0.0;
#endif

    float zoom = max(P_ZOOM, 0.1);
    float rings = max(1.0, floor(TAU * max(P_DENSITY, 0.05) + 0.5));
    float period = TAU / rings;
    // travel advances at 0.1 * speed (default 0.3) per second.
    float clock = 9.0 + wl_time() * 0.03;
    float phase = mod(clock, 16.0 * period);
    float twinkle_clock = mod(clock, TAU);
    float width = 5e-4 * max(P_STREAK_WIDTH, 0.05);
    float tail = 19.0 / max(P_STREAK_LENGTH, 0.05);
    int columns = P_STREAK_WIDTH > 1.0 ? 2 : 1;

    vec2 screen = (2.0 * frag - view) / view.x;
    vec2 p = screen;

    vec3 dir = vec3(p, zoom);
    vec3 exits = (1.0 - CAMERA * sign(dir)) / max(abs(dir), vec3(1e-6));
    float u = min(exits.x, min(exits.y, exits.z));
    for (int i = 0; i < 5; i++) {
        vec3 h = CAMERA + u * dir;
        vec3 h2 = h * h;
        u -= (dot(h2, h2) - 1.0) / (4.0 * dot(dir, h2 * h));
    }
    vec3 hit = CAMERA + u * dir;
    vec3 normal = hit * hit * hit;
    float facing = dot(normal, dir);
    vec3 stepX = u * (vec3(1.0, 0.0, 0.0) - dir * (normal.x / facing));
    vec3 stepY = u * (vec3(0.0, 1.0, 0.0) - dir * (normal.y / facing));
    float ring = hit.y * hit.y + hit.z * hit.z;
    float theta = atan(hit.z, hit.y);
    mat2 jacobian = mat2(
        stepX.x,
        (hit.y * stepX.z - hit.z * stepX.y) / ring,
        stepY.x,
        (hit.y * stepY.z - hit.z * stepY.y) / ring) * (2.0 / view.x);

    vec2 gradX = vec2(jacobian[0][0], jacobian[1][0]);
    vec2 gradTheta = vec2(jacobian[0][1], jacobian[1][1]);
    float scale = max(length(gradX), 1e-8);
    float det = max(abs(gradX.x * gradTheta.y - gradX.y * gradTheta.x), 1e-12);
    float along = scale / det;
    float skew = dot(gradX, gradTheta) / det;
    float spacing = SPACING / scale;
    float detail = smoothstep(1.6, 3.2, spacing);

    vec3 total = vec3(0.0);
    vec3 halo_sum = vec3(0.0);
    if (detail > 0.0) {
        float halfWidth = min(width / scale, 0.2 * spacing);
        float headRadius = min(2.0 * width / scale, 0.35 * spacing);
        float headReach = 4.0 * max(0.6 * headRadius, 0.7);
        // Bloom halo: the blurred mips reach ~6..48 px; keep it inside the
        // column window so neighbouring columns don't seam.
        float haloR = min(9.0 + 3.0 * headRadius, 0.45 * spacing);
        float haloReach = max(headReach, 2.5 * haloR);
        float edge = columns > 1 ? 1.0 : 0.5;
        float fog = exp(-max(u * length(dir) - 0.6, 0.0) * 1.6 * P_DEPTH);

        for (int layer = 0; layer < 16; layer++) {
            if (layer >= P_STREAK_COUNT) break;
            float x = hit.x + float(layer) * SPACING / 8.0;
            float nearest = floor(x / SPACING + 0.5);
            float side = x >= nearest * SPACING ? 1.0 : -1.0;
            for (int k = 0; k < 2; k++) {
                if (k >= columns) break;
                float column = nearest + side * float(k);
                float across = (x - column * SPACING) / scale;
                vec2 sd = vec2(column + 0.37, float(layer) * 17.31 + 5.11);
                float hue = wl_hash12(sd);
                float speedv = wl_hash12(sd + vec2(91.7, 13.3));
                float offs = wl_hash12(sd + vec2(-47.1, 211.9));
                float delta = theta - phase * (1.0 + floor(speedv * 16.0) / 16.0) - offs * period;
                delta -= period * floor(delta / period + 0.5);
                float ahead = delta * along - across * skew;
                if (abs(ahead) > haloReach && (delta > 0.0 || -delta * tail > 3.2)) continue;
                float behind = max(-delta, 0.0);
                float fall = exp(-tail * behind) * (1.0 - smoothstep(0.5, 0.95, behind / (0.5 * period)));
                fall *= 1.0 - smoothstep(1.2, 3.2, tail * behind);
                float reach = length(vec2(across, ahead));
                float trail = stroke(ahead > 0.0 ? reach : abs(across), halfWidth * mix(0.4, 1.0, fall)) * fall;
                float glint = orb(reach, headRadius) * mix(3.0, 1.8, lightm);
                float weight = mix(1.5, 1.0 + sin(twinkle_clock + 7.0 * hue + 4.0), P_TWINKLE);
                float window = 1.0 - smoothstep(0.72 * edge, edge, abs(across) / spacing);
                vec3 color = streak_color(hue);
                vec3 hot = mix(color, vec3(1.0), P_HEAD_GLOW * (1.0 - lightm));
                total += (trail * color + glint * hot) * weight * window;

                // Halo: energy of head + tail spread over haloR pixels.
                float hd = reach / haloR;
                float hh = exp(-hd * hd) * (headRadius * headRadius) / (haloR * haloR) * 2.2;
                float td = abs(across) / haloR;
                float ht = exp(-td * td) * fall * halfWidth / haloR * 0.9 * (ahead > 0.0 ? exp(-ahead * ahead / (haloR * haloR)) : 1.0);
                float hw = 1.0 - smoothstep(0.6 * edge, edge, abs(across) / spacing);
                halo_sum += (ht * color + hh * hot * 3.0) * weight * hw;
            }
        }
        total *= fog * detail;
        halo_sum *= fog * detail;
    }

    vec3 light = total;
    light += halo_sum * P_BLOOM * 0.9 * (lightm > 0.5 ? 0.2 : 1.0);
    light *= P_GLOW;

    vec2 uv = frag / view;
#if !P_LIGHT_MODE
    if (P_BACKGROUND_GLOW > 0.0) {
        float aspect = view.x / view.y;
        vec2 q = vec2((uv.x - 0.5) * aspect, 1.04 - uv.y);
        float spread = 0.34 * max(aspect, 0.75);
        float core = exp(-q.x * q.x / (spread * spread) - q.y * q.y / 0.06);
        float halo = exp(-q.x * q.x / (3.6 * spread * spread) - q.y * q.y / 0.18);
        light += lin(P_TOP_COLOR) * P_BACKGROUND_GLOW * (core * 0.85 + halo * 0.3);
    }
#endif

    vec3 lit = max(light, 0.0);
    vec3 mapped = sqrt(1.0 - exp(-1.3 * lit * lit / (lit + 0.025)));
    float peak = max(mapped.r, max(mapped.g, mapped.b));

    float mask = 1.0;
    float fedge = P_FADE * 0.5;
    if (fedge > 0.0) {
        vec2 fu = clamp(uv, 0.0, 1.0);
        mask *= smootherstep01(fu.x / fedge) * smootherstep01((1.0 - fu.x) / fedge);
        mask *= smootherstep01(fu.y / fedge) * smootherstep01((1.0 - fu.y) / fedge);
    }
    float grain = (fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;

    vec4 premul;
#if P_LIGHT_MODE
    float coverage = clamp(smoothstep(0.0, 0.5, peak) * mask * 0.94 + grain, 0.0, 1.0);
    vec3 chroma = sqrt(clamp(lit / max(max(lit.r, max(lit.g, lit.b)), 1e-6), 0.0, 1.0));
    chroma = pow(chroma, vec3(1.35));
    chroma /= max(max(chroma.r, max(chroma.g, chroma.b)), 1e-4);
    float saturation = 1.0 - min(chroma.r, min(chroma.g, chroma.b));
    chroma = mix(vec3(0.3), chroma, smoothstep(0.08, 0.35, saturation));
    premul = vec4(chroma * coverage, coverage);
#else
    vec3 color = clamp(mapped + grain, 0.0, 1.0) * mask;
    premul = vec4(color, max(color.r, max(color.g, color.b)));
#endif
    gl_FragColor = vec4(clamp(wl_over(P_BACKGROUND, premul), 0.0, 1.0), 1.0);
}
