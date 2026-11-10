// Gradient Waves — ported from React Bits <GradientWaves /> (see ../LICENSE-REACTBITS.md).
// Same height-field ray march and shading as the original. Pointer look /
// light steering, click swells and the intro are dropped.

const float FAR = 4000.0;
const float CAMERA_HEIGHT = 30.0;

#if P_QUALITY == 0
const int STEPS = 56;
#elif P_QUALITY == 2
const int STEPS = 120;
#else
const int STEPS = 84;
#endif

float g_time;

vec3 surface(vec2 xy) {
    vec2 freq = vec2(P_WAVE_SCALE / 7.0, (P_WAVE_SCALE * P_WAVE_RATIO) / 3.0);
    vec4 tc = vec4(g_time / 0.130, g_time / 0.810, g_time / 0.200, g_time / 0.710);
    float a = xy.x + tc.x;
    float swellPhase = (xy.y + a) / 20.0 + tc.y;
    float bend = P_SWELL * cos(swellPhase) / 20.0;
    float mx = a + P_SWELL * sin(swellPhase);
    float crossPhase = xy.x / 23.0 + tc.w;
    float my = xy.y - tc.z + P_TURBULENCE * cos(crossPhase);
    float twist = -P_TURBULENCE * sin(crossPhase) / 23.0;
    float cx = cos(mx * freq.x);
    float cy = cos(my * freq.y);
    float amp = P_AMPLITUDE;
    float h = (sin(mx * freq.x) + sin(my * freq.y)) * amp + P_HEIGHT;
    float dx = amp * (freq.x * cx * (1.0 + bend) + freq.y * cy * twist);
    float dy = amp * (freq.x * cx * bend + freq.y * cy);
    return vec3(h, dx, dy);
}

float march(vec3 origin, vec3 dir, float stride) {
    float dist = 0.0;
    for (int i = 0; i < STEPS; i++) {
        float gap = origin.z + dist * dir.z - surface(origin.xy + dist * dir.xy).x;
        if (abs(gap) < 0.002 * dist + 0.01) break;
        dist += stride * gap;
        if (!(abs(dist) < FAR)) return FAR;
    }
    return dist;
}

void main() {
    g_time = wl_time() * 0.4;
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float c;
    float s;
    float vfov = (3.14159 / 2.3) / max(P_ZOOM, 0.05);
    vec3 cam = vec3(0.0, 0.0, CAMERA_HEIGHT);
    vec2 uv = frag / res - 0.5;
    uv.x *= res.x / res.y;
    uv.y *= -1.0;

    vec3 dir = vec3(0.0, 0.0, -1.0);
    float ulen = length(uv);
    float xrot = vfov * ulen;
    c = cos(xrot);
    s = sin(xrot);
    dir = mat3(1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c) * dir;
    vec2 nuv = ulen > 1e-5 ? uv / ulen : vec2(1.0, 0.0);
    dir = mat3(nuv.x, -nuv.y, 0.0, nuv.y, nuv.x, 0.0, 0.0, 0.0, 1.0) * dir;
    float tilt = clamp(P_TILT, 10.0, 85.0) * 3.14159265 / 180.0;
    c = cos(tilt);
    s = sin(tilt);
    dir = mat3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c) * dir;

    float slope = P_AMPLITUDE * ((P_WAVE_SCALE / 7.0) * (1.0 + abs(P_SWELL) / 20.0)
        + ((P_WAVE_SCALE * P_WAVE_RATIO) / 3.0) * (1.0 + abs(P_TURBULENCE) / 23.0));
    float stride = 0.9 / max(1.0, slope / 1.5);

    float dist = march(cam, dir, stride);
    vec3 pos = cam + dist * dir;
    vec3 field = surface(pos.xy);
    vec3 view = -dir;

    float ang = P_LIGHT_ANGLE * 3.14159265 / 180.0;
    float elevation = 0.52;
    vec3 sun = vec3(cos(elevation) * cos(ang), cos(elevation) * sin(ang), sin(elevation));

    float crest = clamp((field.x - P_HEIGHT) / max(2.0 * P_AMPLITUDE, 0.001) * 0.5 + 0.5, 0.0, 1.0);
    vec3 body = mix(P_WAVE_COLOR, P_CREST_COLOR, smoothstep(0.45, 1.0, crest) * 0.5);
    vec3 normal = normalize(vec3(-field.yz * (1.0 + 1.5 * P_RELIEF), 1.0));
    float diffuse = clamp(dot(normal, sun) * 0.5 + 0.5, 0.0, 1.0);
    float lightness = mix(1.0, diffuse * diffuse * (3.0 - 2.0 * diffuse), P_RELIEF);
    float sheen = pow(clamp(dot(normal, normalize(sun + view)), 0.0, 1.0), 14.0) * P_SHEEN * (0.4 + 0.6 * P_RELIEF);
    float rim = pow(1.0 - clamp(dot(normal, view), 0.0, 1.0), 3.0);

    vec3 col;
    float alpha;
    float haze = 1.0 - exp(-dist / (max(P_FOG_DEPTH, 0.5) * 5.0));
    float fade = 1.0 - smoothstep(0.35, 1.0, haze);
#if P_LIGHT_MODE
    vec3 hue = mix(P_HORIZON_COLOR, body, 0.35 + 0.65 * lightness);
    float density = mix(0.16, 0.9, 1.0 - lightness);
    density *= 1.0 - clamp(sheen, 0.0, 1.0) * 0.85;
    density *= 1.0 - 0.5 * smoothstep(0.0, 1.0, haze);
    col = hue;
    alpha = fade * clamp(density * P_BRIGHTNESS, 0.0, 1.0);
#else
    vec3 lit = mix(P_HORIZON_COLOR * 0.18, body, lightness) + P_CREST_COLOR * sheen;
    lit = mix(lit, P_HORIZON_COLOR, rim * 0.15 * P_RELIEF);
    col = mix(lit, P_HORIZON_COLOR, smoothstep(0.0, 1.0, haze)) * 0.85 * P_BRIGHTNESS;
    alpha = fade;
#endif

    col = clamp(col, 0.0, 1.0);
    float grain = (wl_hash12(frag + fract(wl_time()) * 64.0) - 0.5) * P_GRAIN;
    alpha = clamp(alpha * (1.0 + grain), 0.0, 1.0);
    gl_FragColor = vec4(wl_over(P_BACKGROUND, vec4(col * alpha, alpha)), 1.0);
}
