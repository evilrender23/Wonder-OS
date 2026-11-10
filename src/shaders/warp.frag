#version 100
// Fragment stage of the warp renderer: one premultiplied texture stretched
// over a deformed mesh.

// Compiled with DERIVATIVES defined where the driver has them; without them
// the picture is still right, only less clean where it is squeezed hard.
#ifdef DERIVATIVES
#extension GL_OES_standard_derivatives : enable
#endif

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform sampler2D tex;
uniform float alpha;
// Damage rectangle in frame pixels, (x0, y0, x1, y1): nothing outside it may
// be touched, since the frame behind it is not being repainted.
uniform vec4 clip;
// Texels the picture occupies, and texels the texture was allocated with.
uniform vec2 used;
uniform vec2 alloc;

// A leaning 3D window's light. `lit` is the window's frame within the
// picture, (u0, v0, u1, v1); `light` is the brightness slope across that
// frame along x and y, then where the glint's streak sits across it and how
// bright it is. All zero for everything else.
uniform vec4 light;
uniform vec4 lit;

varying vec2 v_uv;
varying vec2 v_pos;

void main() {
    if (v_pos.x < clip.x || v_pos.y < clip.y || v_pos.x >= clip.z || v_pos.y >= clip.w) {
        discard;
    }
    vec2 texel = v_uv * used;
    vec2 lo = vec2(0.5);
    vec2 hi = used - vec2(0.5);
#ifdef DERIVATIVES
    // Texels one frame pixel spans, along each of the frame's axes.
    vec2 dx = dFdx(texel);
    vec2 dy = dFdy(texel);
    vec2 span = max(vec2(length(vec2(dx.x, dy.x)), length(vec2(dx.y, dy.y))), vec2(1.0));
    // Where the picture is squeezed a pixel covers many texels, and one
    // bilinear tap would pick among them at random. Spread four along the
    // squeezed direction; unsqueezed the spread is nil and the picture stays
    // as sharp as it was.
    vec2 axis = dot(dx, dx) > dot(dy, dy) ? dx : dy;
    axis *= max(0.0, 1.0 - 1.0 / max(length(axis), 1.0));
    vec4 color = 0.25 * (texture2D(tex, clamp(texel - 0.375 * axis, lo, hi) / alloc)
        + texture2D(tex, clamp(texel - 0.125 * axis, lo, hi) / alloc)
        + texture2D(tex, clamp(texel + 0.125 * axis, lo, hi) / alloc)
        + texture2D(tex, clamp(texel + 0.375 * axis, lo, hi) / alloc));
#else
    vec2 span = vec2(1.0);
    vec4 color = texture2D(tex, clamp(texel, lo, hi) / alloc);
#endif
    // The mesh overhangs the picture on every side. Coverage ramps across
    // one frame pixel there (one texel where the picture is not squeezed), so
    // a bent edge is antialiased even where the picture is opaque right up to
    // its border, and an unbent one lands exactly on its pixels.
    if (lit.z > lit.x && lit.w > lit.y) {
        vec2 f = (v_uv - lit.xy) / (lit.zw - lit.xy);
        if (f.x >= 0.0 && f.y >= 0.0 && f.x <= 1.0 && f.y <= 1.0) {
            // The near side catches the light and the far one falls into
            // shade; a soft diagonal streak crosses the card as it turns.
            float shade = 1.0 + dot(f - vec2(0.5), light.xy);
            float across = f.x + 0.35 * (f.y - 0.5) - light.z;
            float glint = light.w * exp(-across * across / 0.02);
            // Premultiplied: lit colour never exceeds its own coverage.
            color.rgb = min(color.rgb * shade + vec3(glint * color.a), vec3(color.a));
        }
    }
    vec2 edge = clamp(min(texel, used - texel) / span + 0.5, 0.0, 1.0);
    gl_FragColor = color * (alpha * edge.x * edge.y);
}
