// Topography — ported from React Bits <Topography /> (see ../LICENSE-REACTBITS.md).
// The instanced terrain mesh becomes a height-field ray march (coarse steps
// on a 4-noise relief, bisection, then the full relief + fine detail at the
// hit). Contour lines, major lines, sun shading, perspective width, depth of
// field and fog follow the original fragment shader; the 4-level bloom is an
// analytic halo around each contour. Cursor lift/trace/light, click pulses,
// markers, glitches and the intro are dropped.

#ifndef TOPO_STEPS
#define TOPO_STEPS 20
#endif
#ifndef TOPO_REFINE
#define TOPO_REFINE 3
#endif
#ifndef TOPO_MARCH_FULL
#define TOPO_MARCH_FULL 1
#endif

const float FOV = 0.32;
const float ORBIT = 9.0;
const float HEIGHT = 1.7;
const float FREQUENCY = 0.45;

vec2 g_morph;
float g_freq;

float corner(vec2 c, vec2 f) {
    vec2 g = wl_hash22(c + vec2(0.5, 0.5)) * 2.0 - 1.0;
    return dot(g, f);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = corner(i, f);
    float b = corner(i + vec2(1.0, 0.0), f - vec2(1.0, 0.0));
    float d = corner(i + vec2(0.0, 1.0), f - vec2(0.0, 1.0));
    float e = corner(i + vec2(1.0, 1.0), f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(d, e, u.x), u.y);
}

vec2 octave(vec2 p, vec2 shift) {
    return vec2(1.6 * p.x - 1.2 * p.y, 1.2 * p.x + 1.6 * p.y) + shift;
}

float shape(float n) {
    float s = log(1.0 + exp(7.0 * (3.0 * n + 0.05))) / 7.0;
    return s * s * 1.4 + 0.1 * n;
}

// relief() of the component.
float relief(vec2 t) {
    vec2 p = t * g_freq;
    vec2 w = vec2(noise(p * 0.42 + g_morph), noise(p * 0.42 - g_morph.yx + vec2(5.2, 1.3)));
    p += w * 0.8;
    float n = noise(p) * 0.55;
    p = octave(p, vec2(7.1, 3.7));
    n += noise(p) * 0.15;
    p = octave(p, vec2(1.9, 8.3));
    n += noise(p) * 0.04;
    return shape(n);
}

// Cheaper relief for the march: drops the small octaves.
float relief_march(vec2 t) {
    vec2 p = t * g_freq;
    vec2 w = vec2(noise(p * 0.42 + g_morph), noise(p * 0.42 - g_morph.yx + vec2(5.2, 1.3)));
    p += w * 0.8;
    float n = noise(p) * 0.55;
#if TOPO_MARCH_FULL
    p = octave(p, vec2(7.1, 3.7));
    n += noise(p) * 0.15;
#endif
    return shape(n);
}

float smoother(float x) {
    x = clamp(x, 0.0, 1.0);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

float stroke(float dist, float width, float soft, float density, float crowd) {
    float cover = max(0.0, min(min(width, soft), 0.5 * (width + soft) - dist)) / soft;
    return mix(cover, min(width * density, 1.0) * 0.6, crowd);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float time = wl_time();
    float aspect = res.x / res.y;

    g_freq = FREQUENCY / P_SCALE;
    float morph = time * P_MORPH * 0.12;
    g_morph = vec2(morph * 0.37, morph * 0.23);
    vec2 dirv = vec2(0.0, 1.0);
#if P_DIRECTION == 1
    dirv = vec2(0.0, -1.0);
#elif P_DIRECTION == 2
    dirv = vec2(-1.0, 0.0);
#elif P_DIRECTION == 3
    dirv = vec2(1.0, 0.0);
#endif
    vec2 travel = dirv * time * 0.25 * 0.8;

    // ---- camera (camera() + viewProjection()) ----
    float pitch = min(clamp(P_TILT, 12.0, 90.0) * 0.017453292, 1.5707963 - 1e-4);
    float cp = cos(pitch);
    vec3 f = vec3(0.0, -sin(pitch), cp);
    vec3 r = vec3(1.0, 0.0, 0.0);
    vec3 u = cross(f, r);
    float height = P_ELEVATION * HEIGHT;
    float lift = height * 0.18;
    float clearance = height * 1.3 + 0.9;
    float distance = max(ORBIT, (clearance - lift) / sin(pitch));
    vec3 eye = vec3(-f.x * distance, lift - f.y * distance, -f.z * distance);
    float lens = clamp(0.4877 / aspect, FOV, 0.6);

    // groundDepth(cam, ±1) for the near-flattening and fog ranges.
    vec3 db = normalize(f - u * lens);
    vec3 dt = normalize(f + u * lens);
    float bottom = db.y > -1e-3 ? 1e4 : (eye.y / -db.y) * dot(db, f);
    float topd = dt.y > -1e-3 ? 1e4 : (eye.y / -dt.y) * dot(dt, f);
    float top = min(topd, distance * 3.2);
    float horizon = max(max(top, bottom * 1.6), distance * 1.4);
    float fadeEnd = horizon + (bottom * 1.8 - horizon) * P_FOG;
    float fadeStart = fadeEnd * 0.7 + (bottom * 0.7 - fadeEnd * 0.7) * P_FOG;
    vec3 nearR = vec3(bottom * 0.9, distance, P_FOREGROUND);

    vec2 ndc = frag / res * 2.0 - 1.0;
    vec3 d = normalize(f + r * ndc.x * lens * aspect + u * ndc.y * lens);
    float along = dot(d, f);

    // ---- ray march the flattened height field ----
    // Height-gap stepping (like the component's own pick()), then bisection
    // and a final secant step so the hit varies smoothly between pixels.
    float hmax = height * 1.75 + 1e-3;
    float tEnter = d.y < -1e-4 ? max((eye.y - hmax) / -d.y, 0.05) : 0.05;
    float tGround = d.y < -1e-4 ? eye.y / -d.y : 1e4;
    float tExit = min(tGround, fadeEnd * 1.05 / max(along, 1e-3));
    bool hit = false;
    float tHit = tExit;
    if (tExit > tEnter && height > 1e-4) {
        float tt = tEnter;
        float lo = tEnter;
        float glo = hmax;
        float hi = tExit;
        float ghi = -1.0;
        bool found = false;
        float slope = abs(d.y) + 0.9 * length(d.xz);
        for (int i = 0; i < TOPO_STEPS; i++) {
            vec3 p = eye + d * tt;
            float depth = dot(p - eye, f);
            float x = clamp((depth - nearR.x) / max(nearR.y - nearR.x, 1e-3), 0.0, 1.0);
            float settle = mix(nearR.z, 1.0, smoother(x));
            float gap = p.y - relief_march(p.xz + travel) * settle * height;
            if (gap <= 0.0) {
                hi = tt;
                ghi = gap;
                found = true;
                break;
            }
            lo = tt;
            glo = gap;
            tt += clamp(gap / slope, 0.02 + 0.004 * tt, 1.5);
            if (tt >= tExit) break;
        }
        if (found) {
            for (int i = 0; i < TOPO_REFINE; i++) {
                float mid = lo + (hi - lo) * clamp(glo / (glo - ghi), 0.15, 0.85);
                vec3 p = eye + d * mid;
                float depth = dot(p - eye, f);
                float x = clamp((depth - nearR.x) / max(nearR.y - nearR.x, 1e-3), 0.0, 1.0);
                float settle = mix(nearR.z, 1.0, smoother(x));
                float gap = p.y - relief_march(p.xz + travel) * settle * height;
                if (gap <= 0.0) { hi = mid; ghi = gap; } else { lo = mid; glo = gap; }
            }
            tHit = lo + (hi - lo) * glo / max(glo - ghi, 1e-6);
            hit = true;
        } else {
            tHit = tGround;
            hit = tGround < tExit + 1e-3;
        }
    } else if (height <= 1e-4) {
        // Elevation 0 lays the map flat.
        tHit = tGround;
        hit = tGround < tExit + 1e-3;
    }

    // ---- shading (uniform control flow for the derivatives) ----
    vec3 P = eye + d * tHit;
    vec2 spot = P.xz + travel;
    vec2 dp = spot * g_freq * 5.0 + vec2(13.1, 4.7);
    float fine = noise(dp) * 0.6 + noise(octave(dp, vec2(2.3, 6.1))) * 0.3;
    float h = relief(spot) + P_DETAIL * 0.1 * fine;
    float lines = float(P_LINE_COUNT);
    float gx = dFdx(h) * lines;
    float gy = dFdy(h) * lines;
    float depthHit = max(dot(P - eye, f), 0.01);
    // Silhouettes: a depth jump inside the pixel quad is not a slope.
    float jump = max(abs(dFdx(depthHit)), abs(dFdy(depthHit))) / depthHit;
    float seam = smoothstep(0.02, 0.06, jump);

    float lc = h * lines + 0.5;
    float density = max(sqrt(gx * gx + gy * gy), 1e-5);
    float dist = abs(fract(lc + 0.5) - 0.5) / density;
#if P_MAJOR_LINES
    float major = 1.0 - step(0.5, mod(floor(lc + 0.5), 5.0));
#else
    float major = 0.0;
#endif

    float persp = clamp(sqrt(distance / depthHit), 0.6, 1.6);
    float width = P_LINE_WIDTH * persp * (1.0 + 0.6 * major);
    float focusScale = P_DEPTH_OF_FIELD * res.y * 0.016;
    float soft = 1.0 + min(focusScale * abs(1.0 - distance / depthHit), res.y * 0.03);
    float crowd = smoothstep(0.3, 0.75, density * max(width, soft));
    float cover = stroke(dist, width, soft, density, crowd) * (1.0 - seam);

    vec3 dPx = dFdx(P);
    vec3 dPy = dFdy(P);
    vec3 normal = cross(dPy, dPx);
    normal = normal.y < 0.0 ? -normal : normal;
    normal = length(normal) > 1e-8 ? normalize(normal) : vec3(0.0, 1.0, 0.0);
    if (seam > 0.5) normal = vec3(0.0, 1.0, 0.0);
    float sun = clamp(dot(normal, vec3(-0.81, 0.58, -0.02)), 0.0, 1.0);
    float level = mix(0.45, 1.05, smoothstep(0.0, 1.1, h)) * mix(0.35, 1.25, sun * sun) * (1.0 + 0.6 * major);

    float fade = 1.0 - smoother((depthHit - fadeStart) / max(fadeEnd - fadeStart, 1e-3));
    if (!hit) fade = 0.0;
    float lightv = cover * level * fade * P_BRIGHTNESS;

    // Bloom: the component blurs the lines over 6-50 px. Where contours are
    // dense that is their average coverage (width x density); around a lone
    // contour it is a faint Gaussian skirt.
    float halo = 0.0;
    if (P_GLOW > 0.001) {
        float avg = clamp(width * density, 0.0, 1.0);
        float skirt = exp(-dist * dist / 60.0) * min(width, 2.0) * 0.08;
        halo = (avg * 0.85 + skirt) * (1.0 - seam);
    }
    float energy = level * fade * P_BRIGHTNESS;

    vec3 col;
#if P_LIGHT_MODE
    float ink = (1.0 - exp(-lightv * 1.8)) * 0.92;
    vec4 inkc = vec4(P_COLOR * ink, ink);
    float gk = (1.0 - exp(-energy * 1.8)) * 0.92 * halo * P_GLOW * 0.35;
    vec4 glowc = vec4(P_COLOR * gk, gk);
    vec4 premul = inkc + glowc * (1.0 - inkc.a);
    float lum = dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722));
    vec3 page = lum > 0.5 ? P_BACKGROUND : vec3(0.965, 0.968, 0.975);
    col = wl_over(page, premul);
#else
    vec3 c = 1.0 - exp(-P_COLOR * lightv * 1.8);
    float dither = fract(sin(dot(frag, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    c = max(c + dither / 255.0 * step(0.002, max(c.r, max(c.g, c.b))), 0.0);
    vec4 premul = vec4(c, max(c.r, max(c.g, c.b)));
    vec3 bl = (1.0 - exp(-P_COLOR * energy * 1.8)) * halo * P_GLOW * 1.6;
    bl = bl / (1.0 + bl);
    col = wl_over(P_BACKGROUND, premul) + bl;
#endif
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
