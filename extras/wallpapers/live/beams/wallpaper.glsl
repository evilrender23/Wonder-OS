// Beams — ported from React Bits <Beams /> (see ../LICENSE-REACTBITS.md).
// Single pass: the field is evaluated per pixel and the mip-chain bloom of
// the hot glints becomes a few taps along the beam (the glints are bands
// across each beam). Pointer light, intro sweep and quality are dropped.

const float PI = 3.141592653589793;

vec4 permute(vec4 x) {
    return mod(((x * 34.0) + 1.0) * x, 289.0);
}

vec4 taylorInvSqrt(vec4 r) {
    return 1.79284291400159 - 0.85373472095314 * r;
}

vec3 fade3(vec3 t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float cnoise(vec3 P) {
    vec3 Pi0 = floor(P);
    vec3 Pi1 = Pi0 + vec3(1.0);
    Pi0 = mod(Pi0, 289.0);
    Pi1 = mod(Pi1, 289.0);
    vec3 Pf0 = fract(P);
    vec3 Pf1 = Pf0 - vec3(1.0);
    vec4 ix = vec4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
    vec4 iy = vec4(Pi0.yy, Pi1.yy);
    vec4 iz0 = Pi0.zzzz;
    vec4 iz1 = Pi1.zzzz;
    vec4 ixy = permute(permute(ix) + iy);
    vec4 ixy0 = permute(ixy + iz0);
    vec4 ixy1 = permute(ixy + iz1);
    vec4 gx0 = ixy0 / 7.0;
    vec4 gy0 = fract(floor(gx0) / 7.0) - 0.5;
    gx0 = fract(gx0);
    vec4 gz0 = vec4(0.5) - abs(gx0) - abs(gy0);
    vec4 sz0 = step(gz0, vec4(0.0));
    gx0 -= sz0 * (step(0.0, gx0) - 0.5);
    gy0 -= sz0 * (step(0.0, gy0) - 0.5);
    vec4 gx1 = ixy1 / 7.0;
    vec4 gy1 = fract(floor(gx1) / 7.0) - 0.5;
    gx1 = fract(gx1);
    vec4 gz1 = vec4(0.5) - abs(gx1) - abs(gy1);
    vec4 sz1 = step(gz1, vec4(0.0));
    gx1 -= sz1 * (step(0.0, gx1) - 0.5);
    gy1 -= sz1 * (step(0.0, gy1) - 0.5);
    vec3 g000 = vec3(gx0.x, gy0.x, gz0.x);
    vec3 g100 = vec3(gx0.y, gy0.y, gz0.y);
    vec3 g010 = vec3(gx0.z, gy0.z, gz0.z);
    vec3 g110 = vec3(gx0.w, gy0.w, gz0.w);
    vec3 g001 = vec3(gx1.x, gy1.x, gz1.x);
    vec3 g101 = vec3(gx1.y, gy1.y, gz1.y);
    vec3 g011 = vec3(gx1.z, gy1.z, gz1.z);
    vec3 g111 = vec3(gx1.w, gy1.w, gz1.w);
    vec4 norm0 = taylorInvSqrt(vec4(dot(g000, g000), dot(g010, g010), dot(g100, g100), dot(g110, g110)));
    g000 *= norm0.x;
    g010 *= norm0.y;
    g100 *= norm0.z;
    g110 *= norm0.w;
    vec4 norm1 = taylorInvSqrt(vec4(dot(g001, g001), dot(g011, g011), dot(g101, g101), dot(g111, g111)));
    g001 *= norm1.x;
    g011 *= norm1.y;
    g101 *= norm1.z;
    g111 *= norm1.w;
    float n000 = dot(g000, Pf0);
    float n100 = dot(g100, vec3(Pf1.x, Pf0.yz));
    float n010 = dot(g010, vec3(Pf0.x, Pf1.y, Pf0.z));
    float n110 = dot(g110, vec3(Pf1.xy, Pf0.z));
    float n001 = dot(g001, vec3(Pf0.xy, Pf1.z));
    float n101 = dot(g101, vec3(Pf1.x, Pf0.y, Pf1.z));
    float n011 = dot(g011, vec3(Pf0.x, Pf1.yz));
    float n111 = dot(g111, Pf1);
    vec3 fade_xyz = fade3(Pf0);
    vec4 n_z = mix(vec4(n000, n100, n010, n110), vec4(n001, n101, n011, n111), fade_xyz.z);
    vec2 n_yz = mix(n_z.xy, n_z.zw, fade_xyz.y);
    float n_xyz = mix(n_yz.x, n_yz.y, fade_xyz.x);
    return 2.2 * n_xyz;
}

float laneHash(float lane) {
    return fract(sin(lane * 91.3458 + P_VARIATION * 17.17) * 47453.5453);
}

float lobe(float nh, float nl, float nv, float vh, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / (PI * d * d);
    float gv = nl * sqrt(nv * nv * (1.0 - a2) + a2);
    float gl = nv * sqrt(nl * nl * (1.0 - a2) + a2);
    float visibility = 0.5 / max(gv + gl, 0.0001);
    float fresnel = 0.028 + 0.972 * pow(1.0 - vh, 5.0);
    return distribution * visibility * fresnel * nl;
}

// Radiance of one beam (lane) at a point in the beam frame.
float glint(float lane, vec2 local, float t, float cam) {
    float along = local.y / P_WAVE_LENGTH - laneHash(lane) * 60.0;
    float h = 0.01;
    float slope = (cnoise(vec3(0.0, along + h, t)) - cnoise(vec3(0.0, along - h, t))) / (2.0 * h);
    slope *= 0.2 * P_DEPTH;
    float spin = laneHash(lane + 0.37);
    float tilt = ((spin - 0.5) * 0.5 + sin(t * 2.1 + spin * 6.2831) * 0.06) * P_TWIST;
    vec3 normal = normalize(vec3(tilt, -slope, 1.0));
    vec3 view = normalize(vec3(-local, cam));
    float nv = max(dot(normal, view), 0.0001);
    float rough = 0.55 - P_SHARPNESS * 0.5;
    vec3 light = normalize(vec3(0.0, 3.0, 10.0));
    vec3 halfway = normalize(light + view);
    float core = lobe(max(dot(normal, halfway), 0.0), max(dot(normal, light), 0.0), nv,
                      max(dot(view, halfway), 0.0), rough);
    vec3 fill = normalize(vec3(0.0, -3.0 * 1.25, 10.0));
    vec3 fillHalf = normalize(fill + view);
    core += P_FILL_LIGHT * lobe(max(dot(normal, fillHalf), 0.0), max(dot(normal, fill), 0.0), nv,
                                max(dot(view, fillHalf), 0.0), rough);
    return core;
}

float random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453123);
}

float grainNoise(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    float a = random(i);
    float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0));
    float d = random(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

vec3 acesFilmic(vec3 color) {
    mat3 inputMatrix = mat3(
        vec3(0.59719, 0.07600, 0.02840),
        vec3(0.35458, 0.90834, 0.13383),
        vec3(0.04823, 0.01566, 0.83777));
    mat3 outputMatrix = mat3(
        vec3(1.60475, -0.10208, -0.00327),
        vec3(-0.53108, 1.10813, -0.07276),
        vec3(-0.07367, -0.00605, 1.07602));
    color = inputMatrix * (color / 0.6);
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = outputMatrix * (a / b);
    return clamp(color, 0.0, 1.0);
}

float feather(float x, float edge) {
    float t = clamp(x / max(edge, 0.0001), 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    // The field runs its clock at 0.12 per second (speed 1).
    float t = mod(wl_time() * 0.12, 289.0);
    float cam = res.y * 1.866;

    float angle = radians(P_ROTATION);
    float c = cos(angle);
    float s = sin(angle);
    vec2 centered = frag - res * 0.5;
    vec2 local = mat2(c, -s, s, c) * centered;

    float across = local.x / P_BEAM_WIDTH + 0.5;
    float lane = floor(across);
    float u = across - lane;
    float radiance = glint(lane, local, t, cam);

    // Seam between two beams: blend over ~1.5 px like the original.
    float seam = min(u, 1.0 - u) * P_BEAM_WIDTH;
    float other = u < 0.5 ? lane - 1.0 : lane + 1.0;
    if (seam < 1.5) {
        float blend = 0.5 * (1.0 - smoothstep(0.0, 1.5, seam));
        radiance = mix(radiance, glint(other, local, t, cam), blend);
    }
    float hot = max(radiance - 0.18, 0.0);

    // Bloom: mips 2..5 of the hot channel (~4..43 px). The glints are bands
    // across the beam, so taps along the beam carry most of it; the
    // neighbouring beam leaks in near the seam.
    float bloom = 0.0;
    if (P_GLOW > 0.0) {
        float acc = hot * 0.16;
        float wsum = 0.16;
        for (int i = 0; i < 4; i++) {
            float fi = float(i);
            float r = 5.0 * pow(2.2, fi);
            float w = 0.3 - 0.04 * fi;
            float dir = mod(fi, 2.0) < 0.5 ? 1.0 : -1.0;
            vec2 off = vec2(0.0, r * dir);
            acc += min(max(glint(lane, local + off, t, cam) - 0.18, 0.0), 6.0) * w;
            wsum += w;
        }
        bloom = acc / wsum;
        float leak = 0.5 * (1.0 - smoothstep(0.0, 30.0, seam));
        if (leak > 0.01) {
            float side = min(max(glint(other, local, t, cam) - 0.18, 0.0), 6.0);
            bloom = mix(bloom, side, leak * 0.6);
        }
    }

    vec3 tint = pow(P_COLOR, vec3(2.2));
    vec3 light = tint * (radiance + bloom * P_GLOW) * P_BRIGHTNESS;
    light += mix(tint, vec3(1.0), 0.7) * hot * hot * 0.12 * P_BRIGHTNESS;
    vec3 color = wl_to_srgb(acesFilmic(light));
    color -= grainNoise(frag) / 15.0 * P_GRAIN * 1.75;
    color = max(color, 0.0);

    vec2 uv = frag / res;
    float edge = P_FADE * 0.5;
    float mask = feather(uv.x, edge) * feather(1.0 - uv.x, edge) * feather(uv.y, edge) * feather(1.0 - uv.y, edge);
    float dither = (random(frag.yx + 31.7) - 0.5) / 255.0;
    float peak = max(color.r, max(color.g, color.b));

    vec3 result;
    if (P_LIGHT_MODE == 1) {
        float tone = dot(tint, vec3(0.2126, 0.7152, 0.0722));
        float shadeDepth = min(1.0, 0.18 / max(tone, 0.001));
        vec3 ink = pow(min(vec3(1.0), tint * shadeDepth), vec3(1.0 / 2.2));
        float lumBg = dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722));
        vec3 paper = lumBg > 0.5 ? P_BACKGROUND : vec3(1.0);
        float alpha = clamp(peak * 0.9 + dither, 0.0, 1.0) * mask;
        result = wl_over(paper, vec4(ink * alpha, alpha));
    } else {
        color = clamp(color + dither, 0.0, 1.0) * mask;
        result = wl_over(P_BACKGROUND, vec4(color, max(color.r, max(color.g, color.b))));
    }
    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
