// text_caret.glsl — the shapes of the text-caret effect.
// u_kind 0: a filled quad (the travelling caret; corners in u_a, u_b) with an
//           optional halo of u_params.x logical pixels.
// u_kind 1: a rounded-rectangle ring (ripple): u_a = (x, y, w, h),
//           u_params = (corner radius, stroke width).
// u_kind 2: a disc (spark): u_a.xy = centre, u_params.x = radius.
// Coordinates are element-local logical pixels; u_aa_scale converts them to
// output pixels so edges stay one pixel soft at any scale or zoom.
precision highp float;
varying vec2 v_coords;
uniform float alpha;
uniform vec2 size;
uniform float u_kind;
uniform vec4 u_a;
uniform vec4 u_b;
uniform vec4 u_params;
uniform float u_aa_scale;
uniform vec4 u_color;

float sd_rounded_box(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + vec2(r);
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

float seg_dist2(vec2 p, vec2 a, vec2 b) {
    vec2 e = b - a;
    vec2 w = p - a;
    vec2 d = w - e * clamp(dot(w, e) / max(dot(e, e), 1e-6), 0.0, 1.0);
    return dot(d, d);
}

// 1.0 when the edge a→b flips the inside/outside parity of p, else 0.0.
float crossing(vec2 p, vec2 a, vec2 b) {
    vec2 e = b - a;
    vec2 w = p - a;
    bool c0 = p.y >= a.y;
    bool c1 = p.y < b.y;
    bool c2 = e.x * w.y > e.y * w.x;
    return ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) ? 1.0 : 0.0;
}

// Signed distance to a quad, whatever its winding; a mid-flight caret is
// sheared, and may even fold over itself for a frame.
float sd_quad(vec2 p, vec2 a, vec2 b, vec2 c, vec2 d) {
    float d2 = min(min(seg_dist2(p, a, b), seg_dist2(p, b, c)),
                   min(seg_dist2(p, c, d), seg_dist2(p, d, a)));
    float n = crossing(p, a, b) + crossing(p, b, c) + crossing(p, c, d) + crossing(p, d, a);
    float inside = mod(n, 2.0);
    return sqrt(d2) * (1.0 - 2.0 * inside);
}

void main() {
    vec2 p = v_coords * size;
    float coverage;
    if (u_kind < 0.5) {
        float sd = sd_quad(p, u_a.xy, u_a.zw, u_b.xy, u_b.zw);
        coverage = clamp(0.5 - sd * u_aa_scale, 0.0, 1.0);
        if (u_params.x > 0.0) {
            float g = max(sd, 0.0) / u_params.x;
            coverage = max(coverage, 0.35 * exp(-4.0 * g * g) * (1.0 - smoothstep(0.8, 1.0, g)));
        }
    } else if (u_kind < 1.5) {
        vec2 half_size = u_a.zw * 0.5;
        float sd = sd_rounded_box(p - (u_a.xy + half_size), half_size, u_params.x);
        coverage = clamp(0.5 - (abs(sd) - u_params.y * 0.5) * u_aa_scale, 0.0, 1.0);
    } else {
        float sd = length(p - u_a.xy) - u_params.x;
        coverage = clamp(0.5 - sd * u_aa_scale, 0.0, 1.0);
    }
    gl_FragColor = u_color * coverage * alpha;
}
