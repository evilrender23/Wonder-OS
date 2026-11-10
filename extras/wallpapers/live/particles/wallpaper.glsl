// Particles — ported from React Bits <Particles /> (see ../LICENSE-REACTBITS.md).
// Single pass: the instanced 3D point cloud becomes 12 depth slabs; each slab
// is a jittered grid (seen through the same 15° camera) with one possible
// particle per cell, kept with the probability that matches the cloud's
// density and only inside the same stretched ball. Each pixel evaluates the
// 2x2 nearest cells per slab and draws the original sprite (shape, glow,
// twinkle, shimmer, depth of field) back to front. Connections join
// particles of neighbouring cells of a slab. Cursor effects, click bursts
// and the fly-in intro are dropped.

const float FOCAL = 7.5957541; // 1 / tan(7.5°)
const int LAYERS = 12;

float hash3(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec3 ink(vec3 rgb) {
    vec3 lin = pow(rgb, vec3(2.2));
    float tone = dot(lin, vec3(0.2126, 0.7152, 0.0722));
    float depth = min(1.0, 0.06 / max(tone, 0.0001));
    return pow(lin * depth, vec3(1.0 / 2.2));
}

vec3 swatch(float h) {
    float i = floor(h * float(P_COLOR_COUNT));
    vec3 c = i < 0.5 ? P_COLOR1 : (i < 1.5 ? P_COLOR2 : P_COLOR3);
#if P_LIGHT_MODE
    return ink(c);
#else
    return c;
#endif
}

float ray(vec2 q, float aa) {
    float width = 0.14 * max(0.0, 1.0 - q.x);
    return (1.0 - smoothstep(width - aa, width + aa, q.y)) * step(q.x, 1.0);
}

// Slab state shared by the helpers below.
float gA;      // slab depth (distance ahead of the camera)
float gDz;     // slab thickness
float gK;      // cell size, in half-screen-height units
float gSeed;   // slab seed
float gP;      // presence probability per cell
float gEdge;   // slab fade (drift wrap)
float gClock;
float gHalf;   // pixels per unit
vec2 gOff;     // slab grid offset
float gCs;
float gSn;
float gShiftY;

// One particle per cell: centre (slab plane coords), its 4 randoms, and its
// visibility alpha (0 when the cell is empty). Returns the depth it sits at.
float particle(vec2 cell, out vec2 centre, out vec4 r, out float vis) {
    vec2 cm = mod(cell, 1024.0);
    vec3 key = vec3(cm, gSeed);
    float present = hash3(key + vec3(0.37, 0.71, 0.13));
    centre = vec2(0.0);
    r = vec4(0.0);
    vis = 0.0;
    if (present >= gP) return gA;
    r = vec4(hash3(key + vec3(17.1, 3.3, 5.7)), hash3(key + vec3(9.2, 41.7, 13.1)),
             hash3(key + vec3(27.5, 7.9, 33.3)), hash3(key + vec3(5.3, 23.9, 61.7)));
    vec2 jit = vec2(hash3(key + vec3(3.9, 11.3, 7.7)), hash3(key + vec3(13.7, 2.1, 19.3)));
    float hz = hash3(key + vec3(31.1, 17.9, 2.9));

    vec2 bob = vec2(sin(gClock * r.z + 6.28 * r.w) * mix(0.1, 1.5, r.x),
                    sin(gClock * r.y + 6.28 * r.x) * mix(0.1, 1.5, r.w));
    centre = (cell + 0.25 + 0.5 * jit + bob * (0.1 / 1.5)) * gK;

#if P_DRIFT == 3
    float a = gA;
#else
    float a = gA + (hz - 0.5) * gDz;
#endif
    vec2 pxy = centre - gOff;
    vec2 world = pxy * gA / FOCAL;
    float wz = P_CAMERA_DISTANCE - a;
    float S = P_SPREAD;
#if P_DRIFT == 0
    float m = dot(world, world) / (S * S) + wz * wz / (100.0 * S * S);
#elif P_DRIFT == 3
    float m = dot(world, world) / (S * S);
#else
    float m = world.x * world.x / (S * S) + wz * wz / (100.0 * S * S);
#endif
    float edge = gEdge;
#if P_DRIFT == 1 || P_DRIFT == 2
    float spanY = S * 2.0 + 3.0;
    float wy = (gSn * pxy.x + gCs * pxy.y + gShiftY) * gA / FOCAL;
    float ky = wy + spanY * 0.5;
    edge *= smoothstep(0.0, spanY * 0.12, ky) * smoothstep(spanY, spanY * 0.88, ky);
#endif
    edge *= smoothstep(1.0, 4.0, a) * smoothstep(100.0, 90.0, a);
    float on = (m <= 1.0 && a > 0.1) ? 1.0 : 0.0;
    vis = on * edge;
    return a;
}

// Premultiplied sprite of one particle at `d` pixels from its centre.
vec4 sprite(vec2 local, vec4 r, float a, float vis) {
    if (vis <= 0.0) return vec4(0.0);
    float dist = a;
    float diameter = P_SIZE_RANDOMNESS <= 0.0 ? P_BASE_SIZE
        : P_BASE_SIZE * (1.0 + P_SIZE_RANDOMNESS * (r.x - 0.5)) / dist;
    float radius = max(diameter, 1.0) * 0.5;
    float coverage = min(1.0, diameter * diameter);
    float focusDist = P_CAMERA_DISTANCE + 14.0;
    float blur = P_DEPTH_OF_FIELD * 14.0 * max(focusDist - dist, 0.0) / max(dist, 1.0);
#if P_LIGHT_MODE
    float glow = P_GLOW * 0.3;
    float shimmerAmt = 0.0;
#else
    float glow = P_GLOW;
    float shimmerAmt = 1.0;
#endif
    float halo = glow > 0.0 ? radius * 2.5 + 2.0 : 0.0;
    float twinkle = 1.0 - P_TWINKLE * 0.6 * (0.5 + 0.5 * sin(gClock * (6.0 + r.y * 14.0) + r.w * 40.0));
    float focus = radius / (radius + blur);
    float alphaV = min(1.0, coverage * twinkle) * vis * max(focus, 0.35);

#if P_ALPHA_PARTICLES
    float softK = 0.2;
#else
    float softK = 0.0;
#endif
    float soft = blur + 0.7 + radius * softK;
    float d = length(local);
    float shape;
#if P_SHAPE == 1
    float box = max(abs(local.x), abs(local.y));
    shape = 1.0 - smoothstep(radius * 0.86 - soft, radius * 0.86 + soft, box);
#elif P_SHAPE == 2
    float e = max(radius * 0.22, 0.6);
    shape = 1.0 - smoothstep(e - soft, e + soft, abs(d - radius * 0.8));
#elif P_SHAPE == 3
    float reach = radius * 1.6;
    vec2 q = abs(local) / max(reach, 0.0001);
    float aa = soft / max(reach, 0.0001);
    shape = max(max(ray(q, aa), ray(q.yx, aa)), 1.0 - smoothstep(radius * 0.32 - soft, radius * 0.32 + soft, d));
#else
    shape = 1.0 - smoothstep(radius - soft, radius + soft, d);
#endif
    float h = halo > 0.0 ? exp(-d / (halo * 0.35)) * glow * 0.4 : 0.0;
    float alpha = clamp(shape + h * (1.0 - shape), 0.0, 1.0) * alphaV * (1.0 - softK);
    vec2 uv = local / max(radius * 2.0, 0.0001) + 0.5;
    vec3 base = swatch(r.z * 0.999);
    vec3 color = clamp(base + shimmerAmt * 0.2 * sin(uv.yxx + gClock + r.y * 6.28), 0.0, 1.0);
    return vec4(color * alpha, alpha);
}

float segment(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    return length(p - a - ab * t);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float T = wl_time();
    // The cloud's clock runs at the component's default speed (0.1).
    gClock = T * 0.1;
    gHalf = res.y * 0.5;
    vec2 u = (frag - 0.5 * res) / gHalf;

#if P_ROTATION
    float spin = gClock * 0.6;
    float rx = sin(gClock * 0.2) * 0.1;
    float ry = cos(gClock * 0.5) * 0.15;
#else
    float spin = 0.0;
    float rx = 0.0;
    float ry = 0.0;
#endif
    float cs = cos(spin);
    float sn = sin(spin);
    gCs = cs;
    gSn = sn;
    vec2 sway = vec2(sin(ry) * cos(rx), -sin(rx));
#if P_DRIFT == 3
    float travel = T * P_DRIFT_SPEED * 6.0;
#else
    float travel = T * P_DRIFT_SPEED * 0.9;
#endif

    float S = P_SPREAD;
    float cam = P_CAMERA_DISTANCE;
    float count = float(P_COUNT);
#if P_DRIFT == 0
    float aMin = max(1.0, cam - 10.0 * S);
    float aMax = min(100.0, cam + 10.0 * S);
    float dz = (aMax - aMin) / float(LAYERS);
    float density = count / (4.18879 * 10.0 * S * S * S);
#elif P_DRIFT == 3
    float spanZ = S * 20.0 + 3.0;
    float dz = spanZ / float(LAYERS);
    float density = count / (3.14159 * S * S * spanZ);
    float tau = travel / dz;
    float shiftL = fract(tau / float(LAYERS)) * float(LAYERS);
    float i0 = mod(ceil(float(LAYERS) - 0.5 - shiftL), float(LAYERS));
#else
    float aMin = max(1.0, cam - 10.0 * S);
    float aMax = min(100.0, cam + 10.0 * S);
    float dz = (aMax - aMin) / float(LAYERS);
    float density = count / (3.14159 * 10.0 * S * S * (S * 2.0 + 3.0));
#endif
    gDz = dz;
    float k0 = 120.0 / gHalf;

    vec4 acc = vec4(0.0);
    for (int li = 0; li < LAYERS; li++) {
#if P_DRIFT == 3
        float idx = mod(i0 + float(li), float(LAYERS));
        float ph = (idx + 0.5) * dz + travel;
        float cyc = floor(ph / spanZ);
        float kz = ph - cyc * spanZ;
        gA = cam - (kz - spanZ * 0.5);
        gSeed = idx + mod(cyc, 997.0) * float(LAYERS) + float(P_VARIATION) * 131.0;
        gEdge = smoothstep(0.0, spanZ * 0.1, kz);
#else
        float idx = float(li);
        gA = aMax - (idx + 0.5) * dz;
        gSeed = idx + float(P_VARIATION) * 131.0;
        gEdge = 1.0;
#endif
        if (gA < 0.5 || gA > 100.0) continue;
        // Cell size: ~120 px, shrunk when the slab is too dense for one
        // particle per cell.
        gK = min(k0, sqrt(0.9 / max(density * dz, 1e-6)) * FOCAL / gA);
        gP = density * dz * (gK * gA / FOCAL) * (gK * gA / FOCAL);

        vec2 shiftU = (cam - gA) * sway * FOCAL / gA;
#if P_DRIFT == 1
        shiftU.y += mod(travel * FOCAL / gA, gK * 1024.0);
#elif P_DRIFT == 2
        shiftU.y -= mod(travel * FOCAL / gA, gK * 1024.0);
#endif
        vec2 w = u - shiftU;
        vec2 L = vec2(cs * w.x + sn * w.y, -sn * w.x + cs * w.y);
        gOff = fract(gSeed * vec2(0.7548777, 0.5698403) + 0.31) * gK * 31.0;
        L += gOff;
        gShiftY = shiftU.y;

        vec2 g = L / gK;
        vec2 base = floor(g);
        vec2 sd = step(0.5, g - base) * 2.0 - 1.0;

#if P_CONNECTIONS
        {
            vec2 c0; vec2 c1; vec2 c2; vec2 c3;
            vec4 r0; vec4 r1; vec4 r2; vec4 r3;
            float v0; float v1; float v2; float v3;
            particle(base, c0, r0, v0);
            particle(base + vec2(sd.x, 0.0), c1, r1, v1);
            particle(base + vec2(0.0, sd.y), c2, r2, v2);
            particle(base + sd, c3, r3, v3);
            float reachU = P_CONNECTION_DISTANCE / gHalf;
            float lw = 0.0;
            vec3 lc = vec3(0.0);
            for (int e = 0; e < 6; e++) {
                vec2 pa; vec2 pb; float va; float vb; vec4 ra; vec4 rb;
                if (e == 0) { pa = c0; pb = c1; va = v0; vb = v1; ra = r0; rb = r1; }
                else if (e == 1) { pa = c0; pb = c2; va = v0; vb = v2; ra = r0; rb = r2; }
                else if (e == 2) { pa = c0; pb = c3; va = v0; vb = v3; ra = r0; rb = r3; }
                else if (e == 3) { pa = c1; pb = c2; va = v1; vb = v2; ra = r1; rb = r2; }
                else if (e == 4) { pa = c1; pb = c3; va = v1; vb = v3; ra = r1; rb = r3; }
                else { pa = c2; pb = c3; va = v2; vb = v3; ra = r2; rb = r3; }
                float len = length(pb - pa);
                if (va <= 0.02 || vb <= 0.02 || len > reachU) continue;
                float fade = 1.0 - len / reachU;
                float dpx = segment(L, pa, pb) * gHalf;
                float cover = 1.0 - smoothstep(0.5 - 0.6, 0.5 + 0.6, dpx);
                float la = cover * fade * fade * 0.7 * min(va, vb);
                vec3 tint = (swatch(ra.z * 0.999) + swatch(rb.z * 0.999)) * 0.5;
                lc = lc * (1.0 - la) + tint * la;
                lw = lw + la * (1.0 - lw);
            }
            acc = vec4(lc, lw) + acc * (1.0 - lw);
        }
#endif

        // Small (far) sprites stay inside their own cell: test one cell only.
        float rmax = 0.5 * max(P_SIZE_RANDOMNESS <= 0.0 ? P_BASE_SIZE
            : P_BASE_SIZE * (1.0 + 0.5 * P_SIZE_RANDOMNESS) / max(gA - 0.5 * dz, 1.0), 1.0);
        float blurMax = P_DEPTH_OF_FIELD * 14.0 * max(cam + 14.0 - (gA - 0.5 * dz), 0.0) / max(gA - 0.5 * dz, 1.0);
        float extent = rmax * 4.5 + 3.0 + blurMax * 3.0;
        int cells = extent < 0.15 * gK * gHalf ? 1 : 4;
        vec4 layer = vec4(0.0);
        for (int ci = 0; ci < 4; ci++) {
            if (ci >= cells) break;
            vec2 cell = base + vec2(ci == 1 || ci == 3 ? sd.x : 0.0, ci >= 2 ? sd.y : 0.0);
            if (hash3(vec3(mod(cell, 1024.0), gSeed) + vec3(0.37, 0.71, 0.13)) >= gP) continue;
            vec2 c;
            vec4 r;
            float v;
            float a = particle(cell, c, r, v);
            if (v <= 0.0) continue;
            vec4 sp = sprite((L - c) * gHalf, r, a, v);
            layer = sp + layer * (1.0 - sp.a);
        }
        acc = layer + acc * (1.0 - layer.a);
    }

#if P_LIGHT_MODE
    gl_FragColor = vec4(clamp(wl_over(P_PAPER, acc), 0.0, 1.0), 1.0);
#else
    gl_FragColor = vec4(clamp(wl_over(P_BACKGROUND, acc), 0.0, 1.0), 1.0);
#endif
}
