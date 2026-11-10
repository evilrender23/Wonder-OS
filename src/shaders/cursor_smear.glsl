// Motion blur for the pointer: the cursor picture integrated along the path
// it took during the exposure, a quadratic Bézier from where it is now (s = 0)
// to where it was (s = 1). Each pixel averages the picture at TAPS places
// along the path, weighted down toward the tail. The places are shifted by a
// per-pixel fraction of one step, which turns what would be a row of ghost
// cursors into an even streak.
//_DEFINES_

precision highp float;

uniform sampler2D tex;
uniform float alpha;
varying vec2 v_coords;

uniform vec2 quad;       // size of the element, physical px
uniform vec2 tile;       // top-left of the cursor picture at s = 0, in the quad
uniform vec2 used;       // size of the cursor picture, texels
uniform vec2 alloc;      // size of the texture, texels
uniform vec2 control;    // path control point, relative to the head
uniform vec2 tail;       // path end, relative to the head
uniform float flipped;   // 1 when the texture is stored bottom-up
uniform float gain;

const int TAPS = 64;

void main() {
    vec2 p = v_coords * quad;
    if (flipped > 0.5) {
        p.y = quad.y - p.y;
    }
    // Interleaved gradient noise: no two neighbours share a phase.
    float phase = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec4 sum = vec4(0.0);
    for (int i = 0; i < TAPS; i++) {
        float s = (float(i) + phase) / float(TAPS);
        float k = 1.0 - s;
        vec2 at = 2.0 * k * s * control + s * s * tail;
        vec2 t = p - tile - at;
        if (t.x < 0.0 || t.y < 0.0 || t.x > used.x || t.y > used.y) {
            continue;
        }
        vec2 uv = clamp(t, vec2(0.5), used - vec2(0.5)) / alloc;
        if (flipped > 0.5) {
            uv.y = 1.0 - uv.y;
        }
        // Brightest by the cursor, gone at the tail.
        sum += texture2D(tex, uv) * (k * k);
    }
    // The weights average to a third.
    gl_FragColor = sum * (3.0 / float(TAPS)) * gain * alpha;
}
