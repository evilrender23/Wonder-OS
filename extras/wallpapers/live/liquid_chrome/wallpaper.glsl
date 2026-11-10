// Liquid Chrome — ported from React Bits <LiquidChrome /> (see ../LICENSE-REACTBITS.md).
// Single pass: the cursor wave simulation (two extra passes on a float
// texture) only adds wakes and click ripples, so it is dropped; the chrome
// itself is the original fragment shader. The rise-in intro is dropped and
// the reflection sway follows its idle path directly instead of a spring.

float tanh1(float x) {
    return wl_tanh(x);
}

float lamp(vec2 p, vec2 center, vec2 size, float soft) {
    vec2 grown = size + soft;
    vec2 q = (p - center) / grown;
    q *= q;
    float d = sqrt(sqrt(dot(q, q)));
    float edge = soft / min(grown.x, grown.y);
    float shape = (1.0 - smoothstep(1.0 - edge, 1.0 + edge, d)) * (1.0 - 0.45 * d * d);
    return shape * size.x * size.y / (grown.x * grown.y);
}

vec3 studio(vec3 r, float blur, vec2 shift) {
    vec2 p = r.xy / max(r.z + 1.0, 0.05);
    p += shift * smoothstep(0.3, 1.0, length(p));
    float radius = length(p);
    float soft = 0.008 + blur;
    vec2 dir = p / max(radius, 1e-3);
    float key = lamp(p, vec2(-0.25, 0.42), vec2(0.3, 0.12), soft);
    float side = lamp(p, vec2(0.48, 0.12), vec2(0.06, 0.3), soft);
    float ground = lamp(p, vec2(-0.1, -0.55), vec2(0.45, 0.06), soft);
    float rise = smoothstep(-1.0, 1.0, dir.y);
#if P_LIGHT_MODE
    float edge = 0.008 + blur * 0.8;
    float lobe = smoothstep(0.15, 0.85, 0.5 + 0.5 * sin(atan(dir.y, dir.x) * 2.0 + 0.9));
    float tone = mix(2.4, 0.95, smoothstep(0.5 - edge, 1.0 + edge, radius));
    tone = mix(tone, 0.02, smoothstep(0.98 - edge, 1.12 + edge, radius));
    tone = mix(tone, mix(0.03, 0.3, lobe) * (0.8 + 0.4 * rise), smoothstep(1.15 - edge, 1.3 + edge, radius));
    float width = 0.035 + edge;
    float g = (radius - 1.4) / width;
    tone += 2.4 * exp(-g * g) * min(1.0, 0.06 / width) * (0.6 + 0.4 * rise);
    return vec3(tone) * (1.0 - 0.5 * side) * (1.0 - 0.3 * ground) + 0.8 * key;
#else
    float width = 0.015 + soft * 2.0;
    float rim = exp(-abs(radius - 1.0) / width) * 0.031 / width * (0.8 + smoothstep(-0.5, 0.8, p.y));
    float band = 1.0 - 0.95 * smoothstep(1.04 - soft, 1.12 + soft, radius);
    float wall = smoothstep(1.25, 1.55, radius) * (0.6 + 0.4 * rise);
    float streak = smoothstep(0.2, 0.8, 0.5 + 0.5 * sin(atan(dir.y, dir.x) * 3.0 + 0.6));
    vec3 color = vec3(0.004, 0.004, 0.005) + 0.02 * smoothstep(-0.4, 0.6, p.y);
    color += vec3(1.0, 0.97, 0.94) * key * 4.0 + vec3(0.9, 0.95, 1.0) * side * 3.0 + ground * 1.2;
    color += vec3(1.0, 0.98, 0.95) * rim;
    color *= band;
    return color + vec3(mix(0.45, 1.8, streak)) * wall;
#endif
}

vec3 shade(vec2 slope, vec3 v, float blur, vec3 f0, vec2 shift) {
    float steep = length(slope);
    slope *= 3.0 * tanh1(steep / 3.0) / max(steep, 1e-6);
    vec3 n = normalize(vec3(-slope, 1.0));
    vec3 r = reflect(v, n);
    float facing = clamp(dot(n, -v), 0.0, 1.0);
    return studio(r, blur, shift) * (f0 + (1.0 - f0) * pow(1.0 - facing, 5.0));
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    // The component advances its clock by speed (0.2) from a random start.
    float time = wl_time() * 0.2 + 11.0;
    float unit = min(res.x, res.y);
    vec3 tint = pow(P_COLOR, vec3(2.2));
    vec2 shift = vec2(0.22 * sin(time * 1.3), 0.16 * sin(time * 0.9 + 1.7)) * P_TILT;

    vec2 uv = (2.0 * frag - res) / unit;
    vec2 p = uv;
    vec2 rowX = vec2(1.0, 0.0);
    vec2 rowY = vec2(0.0, 1.0);
    float amplitude = P_AMPLITUDE;
    for (int ii = 1; ii < 9; ii++) {
        if (ii > P_DETAIL) break;
        float i = float(ii);
        float a = amplitude / i;
        float soften = exp(0.3 - 0.3 * i);
        float argX = i * P_FREQUENCY_X * p.y + time;
        p.x += a * cos(argX);
        rowX -= a * sin(argX) * i * P_FREQUENCY_X * soften * rowY;
        float argY = i * P_FREQUENCY_Y * p.x + time;
        p.y += a * cos(argY);
        rowY -= a * sin(argY) * i * P_FREQUENCY_Y * soften * rowX;
    }
    float phase = time - p.x - p.y;
    vec2 field = -(rowX + rowY);
    float density = max(length(field), 1e-3);
    vec2 across = field / density;
    float k = 5.0 / (3.0 * P_THICKNESS);
    float sn = sin(phase);
    float cs = cos(phase);
    float th = tanh1(k * sn);
    float bevel = (1.0 - th * th) / tanh1(k);
    float fade = smoothstep(3.0, 9.0, 3.14159 * unit / density);
    float weight = P_DEPTH * fade;
    vec2 slope = k * cs * bevel * weight * across;
    vec2 sweep = -k * bevel * (sn + 2.0 * k * cs * cs * th) * weight * density * 2.0 / unit * across;
    vec2 stepX = dFdx(slope);
    vec2 stepY = dFdy(slope);
    vec2 crease = dot(stepX, stepX) > dot(stepY, stepY) ? stepX : stepY;
    if (dot(crease, crease) > dot(sweep, sweep)) sweep = crease;
    float bend = length(sweep);
    float alpha = smoothstep(0.0, 0.1, length(slope));
    vec3 f0 = mix(vec3(1.0), tint, smoothstep(0.05, 0.6, length(slope))) * 0.92;
    vec3 v = normalize(vec3(uv * 0.08, -1.0));
    float blur = P_ROUGHNESS * 0.35;
    vec3 light;
    if (bend < 0.01) {
        light = shade(slope, v, blur + bend * 0.5, f0, shift);
    } else {
        light = vec3(0.0);
        for (int j = 0; j < 4; j++) {
            float t = -0.375 + 0.25 * float(j);
            light += shade(slope + sweep * t, v, blur + bend * 0.15, f0, shift);
        }
        light *= 0.25;
    }
    vec3 color = pow(aces(light * P_EXPOSURE), vec3(1.0 / 2.2));
    float grain = fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715))));
    color += (grain - 0.5) / 255.0;
    vec4 premul = vec4(clamp(color, 0.0, 1.0) * alpha, alpha);
#if P_LIGHT_MODE
    gl_FragColor = vec4(clamp(wl_over(P_PAPER, premul), 0.0, 1.0), 1.0);
#else
    gl_FragColor = vec4(clamp(wl_over(P_BACKGROUND, premul), 0.0, 1.0), 1.0);
#endif
}
