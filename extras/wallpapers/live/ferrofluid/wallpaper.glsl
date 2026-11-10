// Ferrofluid — ported from React Bits <Ferrofluid /> (see ../LICENSE-REACTBITS.md).
// Single pass: the spring simulation is replaced by its settled state (the
// spike amplitude is the field's target height, computed analytically from
// the swell noise), and the bound texture used for empty-space skipping is
// replaced by the amplitude sampled where the ray enters and leaves the
// spike layer (interpolated along the ray). The spike lattice, the clearance
// stepping, the bisection and the studio lighting are the original's.
// Cursor magnet, click pulses and the intro are dropped.

const float FOV_TAN = 0.3443276132;   // tan(19 deg)
const float G = 7.2551975;
const vec2 E1 = vec2(0.8660254, 0.5);
const vec2 E2 = vec2(-0.8660254, 0.5);
const vec2 E3 = vec2(0.0, -1.0);
const mat2 TURN = mat2(0.9902681, 0.1391731, -0.1391731, 0.9902681);
const float F0 = 0.06;

float gRound;
float gSpan;
float gExponent;
float gWarp;
float gTime;
float gWander;
float gPixelAngle;
float gFar;
float gFogStart;
float gFullHeight;

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec2 flowDir() {
#if P_FLOW_DIRECTION == 0
    return vec2(0.0, -1.0);
#elif P_FLOW_DIRECTION == 1
    return vec2(0.0, 1.0);
#elif P_FLOW_DIRECTION == 2
    return vec2(-1.0, 0.0);
#elif P_FLOW_DIRECTION == 3
    return vec2(1.0, 0.0);
#else
    return vec2(0.0);
#endif
}

float flowField(vec2 p) {
    vec2 q = p / 5.5 + vec2(P_VARIATION * 17.31, P_VARIATION * 9.73);
    vec2 drift = flowDir() * gWander * 0.55;
    vec2 warp = vec2(noise(q * 0.6 + vec2(gWander * 0.07, 3.1)), noise(q * 0.6 - vec2(7.3, gWander * 0.06))) - 0.5;
    vec2 r = q + warp * 1.8 * P_TURBULENCE - drift;
    float n = noise(r) * 0.65 + noise(r * 2.03 + 11.7 - drift * 0.5) * 0.35;
    return smoothstep(0.44, 0.86, n);
}

// Settled spike amplitude (the simulation's target height).
float amplitude(vec2 p) {
    float swell = flowField(p);
    float field = P_MAGNETISM * (0.03 + swell * swell);
    return gFullHeight * min(pow(max(field - 0.04, 0.0), 1.6), 1.0);
}

vec2 lattice(vec2 p) {
    vec2 q = TURN * p;
    float t = gTime * 0.35;
    return q + gWarp * vec2(sin(q.y * 0.9 + t), cos(q.x * 0.8 - t * 0.8));
}

float detail(float t) {
    return smoothstep(3.0, 8.0, 1.0 / max(t * gPixelAngle, 1e-5)) * (1.0 - smoothstep(gFogStart, mix(gFogStart, gFar, 0.7), t));
}

float profile(float u, out float rate) {
    float root = sqrt(max(1.0 - u, 0.0) + gRound * gRound);
    rate = 0.5 / (gSpan * root);
    return clamp(1.0 - (root - gRound) / gSpan, 0.0, 1.0);
}

float spikes(vec2 p, float a, float rise, out float clearance) {
    clearance = 10.0;
    if (a < 0.002) return 0.0;
    vec2 q = lattice(p);
    float u = (cos(G * dot(E1, q)) + cos(G * dot(E2, q)) + cos(G * dot(E3, q)) + 1.5) / 4.5;
    if (a > 0.05) {
        float root = gRound + (1.0 - pow(clamp(rise / a, 0.0, 1.0), 1.0 / gExponent)) * gSpan;
        clearance = sqrt(max(1.0 - u, 0.0) / 8.77) - sqrt(max(root * root - gRound * gRound, 0.0) / 8.77);
    }
    float rate;
    return a * pow(profile(u, rate), gExponent);
}

vec2 spikesSlope(vec2 p, float a, vec2 aSlope) {
    if (a < 0.002) return vec2(0.0);
    vec2 q = lattice(p);
    float c1 = G * dot(E1, q);
    float c2 = G * dot(E2, q);
    float c3 = G * dot(E3, q);
    float u = (cos(c1) + cos(c2) + cos(c3) + 1.5) / 4.5;
    vec2 du = -(G / 4.5) * (sin(c1) * E1 + sin(c2) * E2 + sin(c3) * E3) * TURN;
    float rate;
    float s = max(profile(u, rate), 1e-4);
    float shape = pow(s, gExponent);
    return a * gExponent * shape / s * rate * du + aSlope * shape;
}

float panel(vec3 r, vec3 dir, vec3 up, vec2 extent, float soft) {
    vec3 side = normalize(cross(dir, up));
    vec3 top = cross(side, dir);
    float facing = dot(r, dir);
    if (facing <= 0.0) return 0.0;
    vec2 q = vec2(dot(r, side), dot(r, top)) / facing;
    vec2 e = smoothstep(extent + soft, extent - soft, abs(q));
    return e.x * e.y;
}

vec3 studio(vec3 r, vec3 rd, float soft) {
    vec3 z = vec3(0.0, 0.0, 1.0);
    vec3 col = P_KEY_COLOR * 4.2 * panel(r, normalize(vec3(-0.3, -0.5, 0.81)), z, vec2(0.36, 0.22), 0.08 + soft);
    col += P_SIDE_COLOR * 1.5 * panel(r, normalize(vec3(-0.9, -0.2, 0.38)), z, vec2(0.12, 0.7), 0.07 + soft);
    col += P_SIDE_COLOR * 1.5 * panel(r, normalize(vec3(0.9, -0.1, 0.42)), z, vec2(0.12, 0.7), 0.07 + soft);
    float lift = asin(clamp(r.z, -1.0, 1.0)) - asin(clamp(-rd.z, -1.0, 1.0));
    col += P_FILL_COLOR * 0.45 * smoothstep(0.08 - soft, 0.7 + soft, lift) * smoothstep(1.6, 1.0, lift);
    return col;
}

vec3 lightBackground() {
    return dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722)) > 0.5 ? P_BACKGROUND : vec3(1.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float time = wl_time();
    // wander advances at the default speed 0.5; the lattice runs at 2x that.
    gWander = time * 0.5;
    gTime = gWander * 2.0;

    float edge = clamp((P_SHARPNESS - 1.0) / 4.0, 0.0, 1.0);
    gRound = 1.0 - edge * 0.7;
    gExponent = 1.05 + edge * 1.2;
    gSpan = sqrt(1.0 + gRound * gRound) - gRound;
    gWarp = 0.06 * P_TURBULENCE;
    gFullHeight = 1.2 * P_SPIKE_HEIGHT;

    float worldHeight = 22.0 / P_SCALE;
    float pitch = radians(90.0 - P_TILT);
    float distance = worldHeight / (2.0 * FOV_TAN);
    vec3 forward = vec3(0.0, cos(pitch), -sin(pitch));
    vec3 upward = vec3(0.0, sin(pitch), cos(pitch));
    vec3 camera = vec3(0.0, -distance * cos(pitch), distance * sin(pitch));
    gFar = distance * 2.5;
    gFogStart = distance * 0.95;
    gPixelAngle = 2.0 * FOV_TAN / res.y;

#if P_LIGHT_MODE
    vec3 bg = lightBackground();
#else
    vec3 bg = P_BACKGROUND;
#endif

    vec2 ndc = (frag - 0.5 * res) / (0.5 * res.y);
    vec3 rd = normalize(forward + (ndc.x * vec3(1.0, 0.0, 0.0) + ndc.y * upward) * FOV_TAN);
    float top = gFullHeight + 0.03;
    if (rd.z > -0.004) {
        gl_FragColor = vec4(bg, 1.0);
        return;
    }
    float tTop = max((top - camera.z) / rd.z, 0.0);
    float tEnd = min((-0.05 - camera.z) / rd.z, gFar);
    if (tTop >= tEnd) {
        gl_FragColor = vec4(bg, 1.0);
        return;
    }

    // Amplitude where the ray enters the spike layer and where it meets the
    // floor; spikes in between use the interpolation.
    float aTop = amplitude(camera.xy + rd.xy * tTop);
    float aEnd = amplitude(camera.xy + rd.xy * tEnd);
    float invSpan = 1.0 / max(tEnd - tTop, 1e-4);

    float sink = max(-rd.z, 0.2);
    float glide = max(length(rd.xy), 0.05);
    // Nothing can stick out above the larger of the two amplitudes: start
    // there, and skip the march entirely over bare floor.
    float aMax = max(aTop, aEnd);
    float t = max(tTop, (aMax + 0.02 - camera.z) / rd.z);
    float tPrev = t;
    bool found = false;
    for (int k = 0; k < 72; k++) {
        if (aMax < 0.002) break;
        vec3 pos = camera + rd * t;
        float a = mix(aTop, aEnd, clamp((t - tTop) * invSpan, 0.0, 1.0)) * detail(t);
        float clearance;
        float gap = pos.z - spikes(pos.xy, a, pos.z, clearance);
        if (gap < 0.0) {
            found = true;
            break;
        }
        tPrev = t;
        float pixel = t * gPixelAngle;
        float limit = pos.z > a + 0.02 ? 0.6 / glide : max(clearance * 0.9, pixel) / glide;
        t += max(min(gap * 0.75 / sink, limit), pixel);
        if (t > tEnd) break;
    }

    vec3 pos;
    vec2 slope = vec2(0.0);
    float tHit = min(t, tEnd);
    if (found) {
        float lo = tPrev;
        float hi = t;
        for (int k = 0; k < 5; k++) {
            float mid = 0.5 * (lo + hi);
            vec3 probe = camera + rd * mid;
            float a = mix(aTop, aEnd, clamp((mid - tTop) * invSpan, 0.0, 1.0)) * detail(mid);
            float clearance;
            if (probe.z - spikes(probe.xy, a, probe.z, clearance) < 0.0) hi = mid;
            else lo = mid;
        }
        tHit = hi;
        pos = camera + rd * tHit;
        float fade = detail(tHit);
        float e = 1.0 / 3.0;
        float a0 = amplitude(pos.xy);
        vec2 aSlope = vec2(amplitude(pos.xy + vec2(e, 0.0)) - a0, amplitude(pos.xy + vec2(0.0, e)) - a0) / e * fade;
        slope = spikesSlope(pos.xy, a0 * fade, aSlope);
    } else {
        // Ray reached the floor between spikes: flat mirror.
        tHit = min((0.0 - camera.z) / rd.z, gFar);
        pos = camera + rd * tHit;
    }

    vec3 n = normalize(vec3(-slope, 1.0));
    vec3 r = reflect(rd, n);
    float soft = length(fwidth(r)) * 0.6;
    float facing = max(dot(n, -rd), 0.0);
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - facing, 5.0);
    float rim = pow(1.0 - clamp(abs(dot(rd, n)), 0.0, 1.0), 3.0) * smoothstep(0.85, 0.4, n.z);
    float fogged = 1.0 - smoothstep(gFogStart, gFar, tHit);
    float exposure = 3.0 * P_BRIGHTNESS;

#if P_LIGHT_MODE
    vec3 key = normalize(vec3(-0.45, -0.55, 0.7));
    float lambert = clamp(dot(n, key) / key.z, 0.0, 1.0);
    float diffuse = mix(0.62, 1.0, lambert);
    float room = mix(0.3, 1.0, smoothstep(-0.25, 0.3, r.z));
    room -= 0.3 * rim;
    float lum = mix(diffuse, room, fresnel);
    lum = 1.0 - (1.0 - lum) * clamp(exposure / 3.0, 0.0, 1.5);
    vec3 shadow = mix(vec3(0.0), P_SIDE_COLOR, 0.45);
    vec3 paper = mix(shadow, vec3(1.0), clamp(lum, 0.0, 1.0));
    vec3 shade = mix(vec3(1.0), paper, fogged);
    float cover = 1.0 - min(shade.r, min(shade.g, shade.b));
    gl_FragColor = vec4(wl_over(bg, vec4(shade - (1.0 - cover), cover)), 1.0);
#else
    vec3 shine = 1.0 - exp(-(studio(r, rd, soft) * fresnel + P_FILL_COLOR * rim * 0.35) * exposure);
    vec3 glow = shine * fogged;
    gl_FragColor = vec4(wl_over(bg, vec4(glow, max(glow.r, max(glow.g, glow.b)))), 1.0);
#endif
}
