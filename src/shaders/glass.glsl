//_DEFINES_
// glass.glsl — liquid glass, after React Bits' FluidGlass (MIT + Commons
// Clause, (c) David Haz), reduced from a ray-marched solid to a rounded slab.
//
// Runs where the frost is copied into a window's frost texture: `tex` is the
// already-blurred backdrop (a padded per-window crop or the shared full-output
// one), the destination is the window rect. Inside a rim `u_shape.y` px wide
// the slab's edge curves away, so the frost there is taken from further out —
// what lies just past the edge bends into view — with red and blue bent by
// slightly different amounts (dispersion). The lit edge itself is drawn on
// top of the window by glass_rim.glsl.
//
// All lengths are in texels of `tex`, which are output pixels.
precision highp float;
varying vec2 v_coords;
uniform sampler2D tex;
uniform float alpha;
#if defined(DEBUG_FLAGS)
uniform float tint;
#endif
uniform vec2 u_texsize;   // allocation of `tex`
uniform vec2 u_origin;    // window's top-left in `tex`
uniform vec2 u_size;      // window size
uniform vec4 u_valid;     // x0, y0, x1, y1 of the texels holding real backdrop
uniform vec4 u_shape;     // corner radius, rim width, refraction, dispersion
uniform vec4 u_tint;      // rgb, strength

float sd_rounded_box(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + vec2(r);
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

// Outward unit normal of the rounded box's nearest edge.
vec2 rim_normal(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + vec2(r);
    vec2 n;
    if (q.x > 0.0 && q.y > 0.0) n = normalize(q);
    else if (q.x > q.y) n = vec2(1.0, 0.0);
    else n = vec2(0.0, 1.0);
    return n * vec2(p.x < 0.0 ? -1.0 : 1.0, p.y < 0.0 ? -1.0 : 1.0);
}

vec4 backdrop(vec2 texel) {
    vec2 t = clamp(texel, u_valid.xy + 0.5, u_valid.zw - 0.5);
    return texture2D(tex, t / u_texsize);
}

void main() {
    vec2 texel = v_coords * u_texsize;
    vec2 p = texel - u_origin - u_size * 0.5;
    vec2 half_size = u_size * 0.5;
    float radius = min(u_shape.x, min(half_size.x, half_size.y));
    float rim = max(u_shape.y, 1.0);

    float depth = -sd_rounded_box(p, half_size, radius);
    float t = clamp(depth / rim, 0.0, 1.0);
    // How steeply the slab's surface falls away here: flat inside the rim,
    // steepest at the edge. Smooth at both ends, so the bend has no crease
    // where the rim starts.
    float fall = (1.0 - t) * (1.0 - t) * (1.0 - t * 0.5);
    vec2 n = rim_normal(p, half_size, radius);
    // Outwards: the rim shows what lies just past the edge, compressed.
    vec2 bend = n * u_shape.z * rim * fall;
    vec2 split = n * u_shape.w * (1.5 + rim * 0.3) * fall;

    vec4 g = backdrop(texel + bend);
    vec4 color = g;
    if (u_shape.w > 0.0) {
        color.r = backdrop(texel + bend + split).r;
        color.b = backdrop(texel + bend - split).b;
    }
    // A thin film of the tint, thicker towards the middle like real glass.
    float body = u_tint.a * (0.55 + 0.45 * t);
    color.rgb = mix(color.rgb, color.rgb * u_tint.rgb + u_tint.rgb * 0.06 * color.a, body);
    // The rim catches a little light even where the window covers it.
    color.rgb += vec3(pow(1.0 - t, 3.0) * 0.05) * color.a;

    gl_FragColor = color * alpha;
}
