// Acid Squares — ported from React Bits <AcidSquares /> (see ../LICENSE-REACTBITS.md).
// Single pass: the displaced 210x210 mesh becomes a short ray march of the
// same height field (swells + ripples) from the same camera, then the
// original tile shading runs at the hit point. Ripples are spawned on the
// same schedule analytically; cursor press/lift, click ripples, the view
// following the cursor and the reveal intro are dropped.

const float F_PROJ = 2.3652224; // 1 / tan(0.8 / 2)
const vec2 ORIGIN = vec2(0.0, -3.0);

vec4 gRip[8];
float gClock;
float gSwell;
float gTravel;

float hash3(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float reachOf(vec2 delta, out vec2 grad) {
#if P_SHAPE == 0
    float sq = 1.0;
#else
    float sq = 0.0;
#endif
    vec2 a = abs(delta);
    vec2 dir = vec2(delta.x < 0.0 ? -1.0 : 1.0, delta.y < 0.0 ? -1.0 : 1.0);
    float round_ = length(delta);
    vec2 roundGrad = delta / max(round_, 1e-4);
    float m = max(max(a.x, a.y), 1e-4);
    vec2 k = a / m;
    vec2 k3 = k * k * k;
    float sum = dot(k3, k);
    float root = sqrt(sqrt(sum));
    float square = m * root;
    vec2 squareGrad = dir * k3 / (root * root * root);
    float d = mix(round_, square, sq);
    vec2 g = mix(roundGrad, squareGrad, sq);
    float soft = sqrt(d * d + 0.36);
    grad = g * d / soft;
    return soft - 0.6;
}

vec3 field(vec2 p) {
    float t = gSwell;
    float f = 1.9 / P_WAVE_SCALE;
    vec2 k1 = vec2(0.19, 0.11) * f;
    vec2 k2 = vec2(-0.09, 0.23) * f;
    vec2 k3 = vec2(0.29, -0.17) * f;
    float a1 = dot(p, k1) + t * 0.55;
    float a2 = dot(p, k2) - t * 0.42 + 1.3;
    float a3 = dot(p, k3) + t * 0.74 + 2.1;
    float h = 0.55 * sin(a1) + 0.4 * sin(a2) + 0.22 * sin(a3);
    vec2 g = 0.55 * cos(a1) * k1 + 0.4 * cos(a2) * k2 + 0.22 * cos(a3) * k3;
    float amp = 1.3 * P_WAVES;
    h *= amp;
    g *= amp;
    float wx = 0.55 * P_PULSE;
    float wz = 1.6 * P_PULSE_WIDTH;
    vec2 dg;
    for (int i = 0; i < 8; i++) {
        vec4 r = gRip[i];
        if (r.w <= 0.0) continue;
        float age = gClock - r.z;
        if (age <= 0.0) continue;
        float radius = age * gTravel;
        float life = clamp(1.0 - radius / 46.0, 0.0, 1.0);
        if (life <= 0.0) continue;
        float d = reachOf(p - r.xy, dg);
        float x = (d - radius) / wz;
        float strength = r.w * wx * life * life * smoothstep(0.0, 1.5, radius);
        float bump = exp(-x * x);
        h += bump * strength;
        g += -2.0 * x * bump * strength / wz * dg;
    }
    return vec3(h, g);
}

vec3 light_row(int which) {
    // x, height, z of the light; orbit radii come from light_orbit().
#if P_LIGHTING == 0
    return which == 0 ? vec3(7.0, 4.2, -4.0) : vec3(-8.0, 3.6, 3.0);
#elif P_LIGHTING == 1
    return which == 0 ? vec3(6.0, 1.6, -15.0) : vec3(-7.0, 1.4, -13.0);
#else
    return which == 0 ? vec3(3.0, 7.5, -4.0) : vec3(-3.0, 7.0, -1.0);
#endif
}

vec2 light_orbit() {
#if P_LIGHTING == 0
    return vec2(4.0, 4.0);
#elif P_LIGHTING == 1
    return vec2(5.0, 3.0);
#else
    return vec2(3.0, 3.0);
#endif
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float T = wl_time();
    gClock = T;
    // swell and light orbits advance with the component's speed (0.6).
    gSwell = T * 0.6;
    float orbit = T * 0.36;
    gTravel = 3.0 * max(P_PULSE_SPEED, 0.05) + 1.2;

    // Camera, as cameraFor() with the cursor look at rest.
    float aspect = res.x / res.y;
    float distance_ = 8.0 / P_ZOOM;
    float pitch = 1.5 - P_TILT * 1.26;
    vec3 target = vec3(ORIGIN.x, 0.0, ORIGIN.y);
    vec3 Z = vec3(0.0, sin(pitch), cos(pitch));
    vec3 eye = target + Z * distance_;
    vec3 X = vec3(1.0, 0.0, 0.0);
    vec3 Y = vec3(0.0, cos(pitch), -sin(pitch));

    // Ripple schedule (spawn every interval from t = 1.2, keep the last 8).
#if P_PULSE_ORIGIN == 1
    float interval = 10.0 * P_PULSE_SPACING / gTravel * 0.6;
#else
    float interval = 10.0 * P_PULSE_SPACING / gTravel;
#endif
    float kmax = floor((gClock - 1.2) / interval);
    for (int j = 0; j < 8; j++) {
        float k = kmax - float(j);
        vec2 o = ORIGIN;
        float on = (k >= 0.0 && P_PULSE > 0.0) ? 1.0 : 0.0;
#if P_PULSE_ORIGIN == 1
        vec2 hr = vec2(hash3(vec3(k, 3.1, 9.7)), hash3(vec3(k, 7.3, 1.9)));
        vec2 sp = vec2((0.15 + hr.x * 0.7) * 2.0 - 1.0, 1.0 - (0.25 + hr.y * 0.6) * 2.0);
        vec3 sd = normalize(-Z + sp.x * aspect / F_PROJ * X + sp.y / F_PROJ * Y);
        if (sd.y < -1e-4) {
            float tg = -eye.y / sd.y;
            o = eye.xz + sd.xz * tg;
        } else {
            on = 0.0;
        }
#endif
        gRip[j] = vec4(o, 1.2 + k * interval, on);
    }

    vec2 ndc = frag / res * 2.0 - 1.0;
    vec3 rd = normalize(-Z + ndc.x * aspect / F_PROJ * X + ndc.y / F_PROJ * Y);

    float reach = distance_ / 8.0;
    float far = min(P_DEPTH * 3.2 * reach, 94.0 * 0.62);
    vec2 range = vec2(far * 0.5, far);

    float alpha = 0.0;
    vec3 color = vec3(0.0);
    if (rd.y < -1e-4) {
        float hmax = 1.3 * P_WAVES * 1.17 + 0.55 * P_PULSE + 0.02;
        float ta = max((eye.y - hmax) / -rd.y, 0.0);
        float tb = min((eye.y + hmax) / -rd.y, far + 1.0);
        if (ta < tb) {
            // March the height field, then refine the crossing with a secant.
            float t0 = ta;
            float f0 = eye.y + ta * rd.y - field(eye.xz + rd.xz * ta).x;
            float t1 = tb;
            float f1 = -1.0;
            for (int i = 1; i <= 7; i++) {
                float t = mix(ta, tb, float(i) / 7.0);
                vec3 q = eye + rd * t;
                float f = q.y - field(q.xz).x;
                if (f < 0.0) {
                    t1 = t;
                    f1 = f;
                    break;
                }
                t0 = t;
                f0 = f;
            }
            float th = t0 + (t1 - t0) * f0 / max(f0 - f1, 1e-5);
            vec3 qh = eye + rd * th;
            float fh = qh.y - field(qh.xz).x;
            if (fh > 0.0) { t0 = th; f0 = fh; } else { t1 = th; f1 = fh; }
            th = t0 + (t1 - t0) * f0 / max(f0 - f1, 1e-5);

            vec2 ground = eye.xz + rd.xz * th;
            vec3 f = field(ground);
            vec3 world = vec3(ground.x, f.x, ground.y);

            vec3 n0 = normalize(vec3(-f.y, 1.0, -f.z));
            vec3 tx = normalize(vec3(1.0, f.y, 0.0));
            vec3 tz = normalize(cross(tx, n0));
            tx = normalize(cross(n0, tz));

            float ang = radians(P_ROTATION);
            float c = cos(ang);
            float s = sin(ang);
            mat2 toTile = mat2(c, -s, s, c);
            mat2 fromTile = mat2(c, s, -s, c);
            float tile = 1.2 * P_TILE_SIZE;
            vec2 q = toTile * ground / tile;
            vec2 cell = floor(q);
            vec2 local = q - cell - 0.5;
            float footprint = max(length(fwidth(q)), 1e-5);

            float half_ = 0.5 - P_GAP * 0.5;
            float radius = min(P_ROUNDNESS * half_, half_);
            vec2 dd = abs(local) - (half_ - radius);
            float d = length(max(dd, 0.0)) + min(max(dd.x, dd.y), 0.0) - radius;
            vec2 outward = (dd.x > 0.0 || dd.y > 0.0)
                ? normalize(max(dd, 0.0) * sign(local) + vec2(1e-6))
                : (dd.x > dd.y ? vec2(sign(local.x), 0.0) : vec2(0.0, sign(local.y)));

            float bevelWidth = max(mix(0.04, 0.42, P_SOFTNESS) * half_, footprint * 1.5);
            float edge = clamp((d + bevelWidth) / bevelWidth, 0.0, 1.0);
            float dome = P_BULGE * 0.5 * length(local) / max(half_, 1e-3);
            float theta = edge * edge * mix(0.7, 1.35, P_SOFTNESS) + dome;
            vec2 outwardGround = fromTile * outward;
            vec2 domeGround = fromTile * (local / max(length(local), 1e-4));
            vec3 bendv = normalize(tx * mix(domeGround.x, outwardGround.x, edge)
                                 + tz * mix(domeGround.y, outwardGround.y, edge) + vec3(0.0, 1e-6, 0.0));
            float crowd = smoothstep(0.08, 0.3, footprint);
            theta *= 1.0 - crowd;
            vec3 n = normalize(n0 * cos(theta) + bendv * sin(theta));

            float inside = 1.0 - smoothstep(-footprint * 0.75, footprint * 0.75, d);
            float area = (2.0 * half_) * (2.0 * half_) - (4.0 - 3.14159) * radius * radius;
            inside = mix(inside, area, crowd);

            vec3 rowA = light_row(0);
            vec3 rowB = light_row(1);
            vec2 orb = light_orbit();
            vec3 lightA = vec3(target.x + (rowA.x + sin(orbit * 0.7) * orb.x) * reach,
                               rowA.y * reach + 0.6,
                               target.z + (rowA.z + cos(orbit * 0.5) * orb.y) * reach);
            vec3 lightB = vec3(target.x + (rowB.x + cos(orbit * 0.6) * orb.x) * reach,
                               rowB.y * reach + 0.6,
                               target.z + (rowB.z + sin(orbit * 0.45) * orb.y) * reach);

            vec3 toEye = eye - world;
            float dist = length(toEye);
            vec3 v = toEye / dist;
            vec3 la = lightA - world;
            vec3 lb = lightB - world;
            float da = length(la);
            float db = length(lb);
            la /= da;
            lb /= db;
            float fa = 1.0 / (1.0 + da * da * 0.02);
            float fb = 1.0 / (1.0 + db * db * 0.02);

            float ndv = clamp(dot(n, v), 0.0, 1.0);
            float fresnel = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);
            vec3 ha = normalize(la + v);
            vec3 hb = normalize(lb + v);
            float shine = mix(10.0, 90.0, P_GLOSS);
            float norm = (shine + 8.0) / 60.0;
            float specA = pow(max(dot(n, ha), 0.0), shine) * norm;
            float specB = pow(max(dot(n, hb), 0.0), shine) * norm;
            float sheenA = pow(max(dot(n, ha), 0.0), 8.0);
            float sheenB = pow(max(dot(n, hb), 0.0), 8.0);
            float diffA = max(dot(n, la), 0.0);
            float diffB = max(dot(n, lb), 0.0);
            vec3 r = reflect(-v, n);
            float rim = exp((dot(r, normalize(vec3(0.15, 0.55, -1.0))) - 1.0) * 6.0);
            float occlusion = mix(1.0, 0.35, edge * edge);
            float edgeLight = edge * edge;

#if P_LIGHT_MODE
            // The squares take the page colour (the component asks for the
            // tile colour to match the page), here the light-mode paper.
            float luma = dot(P_PAPER, vec3(0.299, 0.587, 0.114));
            vec3 body = mix(vec3(luma), P_PAPER, 0.5) * 0.96;
            vec3 keyDir = normalize(vec3(-0.35, 0.85, 0.45));
            float shadeK = clamp(dot(n, keyDir) * 0.6 + 0.5, 0.0, 1.0);
            float wa = clamp(diffA * fa * 9.0, 0.0, 1.0);
            float wb = clamp(diffB * fb * 9.0, 0.0, 1.0);
            vec3 hue = (P_ACCENT_COLOR * wa + P_SECONDARY_COLOR * wb) / max(wa + wb, 1e-3);
            vec3 lit = body * mix(vec3(1.0), hue, clamp(wa + wb, 0.0, 1.0) * 0.42) * mix(0.7, 1.05, shadeK);
            lit += vec3(1.0) * (specA * fa + specB * fb) * 2.4 * P_GLOSS;
            lit *= mix(1.0, 0.8, edgeLight * (1.0 - crowd));
            color = lit;
#else
            float luma = dot(P_COLOR, vec3(0.299, 0.587, 0.114));
            vec3 body = mix(vec3(luma), P_COLOR, 0.35) + vec3(0.02);
            vec3 lit = body * 0.25;
            lit += body * (P_ACCENT_COLOR * diffA * fa + P_SECONDARY_COLOR * diffB * fb) * 14.0;
            lit += P_ACCENT_COLOR * (specA * (0.55 + 1.6 * edgeLight) + sheenA * 0.3) * fa * 9.0 * P_GLOSS;
            lit += P_SECONDARY_COLOR * (specB * (0.55 + 1.6 * edgeLight) + sheenB * 0.3) * fb * 9.0 * P_GLOSS;
            lit += mix(P_SECONDARY_COLOR, vec3(1.0), 0.4) * rim * fresnel * 0.35 * P_GLOSS;
            lit *= occlusion;
            color = lit * inside;
#endif
            alpha = inside;
            float fade = 1.0 - wl_smoother((dist - range.x) / max(range.y - range.x, 1e-3));
            alpha *= fade;
        }
    }

    color *= P_BRIGHTNESS;
#if !P_LIGHT_MODE
    color = 1.0 - exp(-color * 1.1);
#endif
    float frame = mod(floor(T * 60.0), 977.0);
    vec2 grainCell = floor(frag);
    float grain = hash3(vec3(grainCell, frame)) + hash3(vec3(grainCell + 17.0, frame + 3.0)) - 1.0;
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
#if P_LIGHT_MODE
    color += grain * P_GRAIN * 0.6;
#else
    color += grain * P_GRAIN * (0.25 + 0.75 * sqrt(clamp(lum, 0.0, 1.0)));
#endif
    color = clamp(color, 0.0, 1.0);

    vec2 uv = frag / res;
    float edgeFade = max(P_FADE * 0.5, 1e-4);
    float mask = P_FADE > 0.0
        ? wl_smoother(uv.x / edgeFade) * wl_smoother((1.0 - uv.x) / edgeFade)
          * wl_smoother(uv.y / edgeFade) * wl_smoother((1.0 - uv.y) / edgeFade)
        : 1.0;
    alpha *= mask;
    vec4 premul = vec4(color * alpha, alpha);
#if P_LIGHT_MODE
    gl_FragColor = vec4(clamp(wl_over(P_PAPER, premul), 0.0, 1.0), 1.0);
#else
    gl_FragColor = vec4(clamp(wl_over(P_BACKGROUND, premul), 0.0, 1.0), 1.0);
#endif
}
