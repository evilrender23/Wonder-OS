// glass_rim.glsl — the lit edge of liquid glass, drawn over a frosted window
// or layer. FluidGlass's rim light (React Bits, MIT + Commons Clause, (c) David
// Haz): brightest where the edge faces the key light and the edge opposite it,
// a hairline at the very edge plus a soft sheen across the rim.
//
// Lengths are in shader-logical units; u_aa_scale turns them into output
// pixels so the hairline stays one pixel wide at any scale or zoom.
precision highp float;
varying vec2 v_coords;
uniform float alpha;
uniform vec2 size;
uniform float u_radius;
uniform float u_bevel;
uniform float u_reflection;
uniform float u_aa_scale;

float sd_rounded_box(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + vec2(r);
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

vec2 rim_normal(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + vec2(r);
    vec2 n;
    if (q.x > 0.0 && q.y > 0.0) n = normalize(q);
    else if (q.x > q.y) n = vec2(1.0, 0.0);
    else n = vec2(0.0, 1.0);
    return n * vec2(p.x < 0.0 ? -1.0 : 1.0, p.y < 0.0 ? -1.0 : 1.0);
}

void main() {
    vec2 half_size = size * 0.5;
    vec2 p = v_coords * size - half_size;
    float radius = min(u_radius, min(half_size.x, half_size.y));
    float d = sd_rounded_box(p, half_size, radius);
    float coverage = clamp(0.5 - d * u_aa_scale, 0.0, 1.0);
    if (coverage <= 0.0) discard;

    float depth_px = max(-d * u_aa_scale, 0.0);
    float t = clamp(-d / max(u_bevel, 0.001), 0.0, 1.0);
    // Key light from the upper left (y grows downwards here).
    vec2 key = normalize(vec2(-0.42, -0.55));
    float facing = abs(dot(rim_normal(p, half_size, radius), key));
    float lit = 0.25 + 0.75 * facing;

    float hairline = exp(-depth_px / 0.9) * 0.55;
    float sheen = pow(1.0 - t, 3.0) * 0.16;
    float a = clamp((hairline + sheen) * lit * u_reflection, 0.0, 1.0) * coverage;

    gl_FragColor = vec4(a) * alpha;
}
