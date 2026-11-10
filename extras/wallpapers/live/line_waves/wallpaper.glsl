// Line Waves — ported from React Bits <LineWaves /> (see ../LICENSE-REACTBITS.md).
// Same single-pass field as the original; the cursor lens, click ripples
// and the draw-in intro are dropped. Output is premultiplied, composited
// over the background colour.

vec2 rotate(vec2 p, float a) {
    float c = cos(a);
    float s = sin(a);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), w.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), w.x), w.y);
}

float smootherstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float bend(vec2 p, float t) {
    vec2 q = p * 2.0 / P_WARP_SCALE;
    float w = 0.6 * sin(q.x * 0.55 + t * 0.4 + 0.6 * sin(q.y * 0.9 - t * 0.28));
    w += 0.21 * sin(q.x * 1.3 - t * 0.33 + q.y * 0.8 + 1.3);
    w += 0.12 * sin(q.y * 2.3 + q.x * 0.9 - t * 0.37 + 0.4);
    return w * P_WARP * P_WARP_SCALE * 0.46;
}

float river(vec2 p, float y, float t) {
    float drift = 0.7 * (0.18 * sin(p.x * 1.6 - t * 0.33 + 0.7) + 0.1 * sin(p.x * 3.1 + t * 0.21));
    float center = 0.5 * (0.2 * sin(t * 0.17) + 0.08 * sin(t * 0.29 + 1.3));
    float width = 0.13 * P_HIGHLIGHT_WIDTH;
    float near = (y - center - drift) / width;
    float far = (y + 0.22 * sin(t * 0.13 + 2.0) - drift * 0.6 + 0.3) / (width * 0.55);
    return max(exp(-near * near), exp(-far * far) * 0.7);
}

vec3 palette(float x) {
    x = fract(x) * 3.0;
    if (x < 1.0) return mix(P_COLOR1, P_COLOR2, smoothstep(0.0, 1.0, x));
    if (x < 2.0) return mix(P_COLOR2, P_COLOR3, smoothstep(1.0, 2.0, x));
    return mix(P_COLOR3, P_COLOR1, smoothstep(2.0, 3.0, x));
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float T = wl_time();
    // The component advances its clock by speed (0.3) * 2.5 from a random
    // start, and the colour cycle by speed * cycleSpeed * 0.05.
    float t = T * 0.75 + 23.0;
    float cycle = T * 0.015 * P_COLOR_CYCLE;
    float rotation = radians(P_ROTATION);

    float aspect = res.x / res.y;
    vec2 uv = frag / res;
    vec2 p = rotate((frag - 0.5 * res) / res.y, rotation);

    float w = bend(p, t);
    float y = p.y + w;
    float phi = y * P_LINE_COUNT;
    float fw = max(fwidth(phi), 1e-5);
    float crowd = smoothstep(0.18, 0.5, fw);

    float fold = smoothstep(1.3, 2.2, fw * res.y / P_LINE_COUNT);
    float shimmer = mix(1.0, smoothstep(0.15, 0.85, noise(vec2(y * 1.6 + t * 0.07, p.x * 0.55 - t * 0.22))), 0.5);
    float lit = max(river(p, y, t), fold * 0.9) * shimmer;
    float light = mix(1.0, 0.07 + 0.93 * lit, P_HIGHLIGHT);

#if P_LIGHT_MODE
    float lightMode = 1.0;
#else
    float lightMode = 0.0;
#endif
    float dist = abs(fract(phi + 0.5) - 0.5) / fw;
    float width = P_LINE_WIDTH * (1.0 + mix(1.5, 0.6, lightMode) * lit * P_HIGHLIGHT);
    float cover = clamp(min(dist + width * 0.5, 0.5) - max(dist - width * 0.5, -0.5), 0.0, 1.0);
    cover = mix(cover, min(width * fw, 1.0), crowd);
    float halo = exp(-dist / 2.2) * (1.0 - crowd) * P_GLOW;
    float thread = mix(1.0, 0.55 + 0.45 * hash(vec2(floor(phi + 0.5), 7.3)), 0.45 * (1.0 - crowd));

    float edge = 1.0;
    if (P_EDGE_FADE > 0.0) {
        float ex = min(uv.x, 1.0 - uv.x) * aspect;
        float ey = min(uv.y, 1.0 - uv.y);
        edge = smootherstep(0.0, P_EDGE_FADE * 0.5, ex) * smootherstep(0.0, P_EDGE_FADE * 0.5, ey);
    }

    vec3 tint = palette(y * 0.3 + p.x * 0.2 + cycle);
    float grain = (fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;

#if P_LIGHT_MODE
    float low = min(tint.r, min(tint.g, tint.b));
    float chroma = max(tint.r, max(tint.g, tint.b)) - low;
    vec3 hue = (tint - low) / max(chroma, 1e-3);
    vec3 ink = mix(vec3(0.08), hue * 0.56 + 0.06, smoothstep(0.04, 0.2, chroma));
    float alpha = (cover + halo * 0.35) * thread * light * P_BRIGHTNESS * edge * 0.78;
    alpha = clamp(alpha + grain, 0.0, 1.0);
    gl_FragColor = vec4(clamp(wl_over(P_PAPER, vec4(ink * alpha, alpha)), 0.0, 1.0), 1.0);
#else
    float strength = (cover + halo) * thread * light * P_BRIGHTNESS * edge;
    vec3 color = clamp(tint * strength, 0.0, 1.0);
    float alpha = max(color.r, max(color.g, color.b));
    color = clamp(color + grain, 0.0, 1.0);
    gl_FragColor = vec4(clamp(wl_over(P_BACKGROUND, vec4(min(color, vec3(alpha)), alpha)), 0.0, 1.0), 1.0);
#endif
}
