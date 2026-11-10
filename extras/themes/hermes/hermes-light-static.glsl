// Hermes Light — canvas background for WonderlandWM.
// Palette from Hermes Agent Desktop's default "nous" theme (light mode):
// chrome #f8f9fe, sidebar #f3f7ff, blue-tinted strokes, Nous blue #0053fd.
// A paper-grain canvas with a two-level dot grid and a small "home" mark at
// the canvas origin. Static (no u_zoom/u_time), so `cache_shader = true` can
// bake it once and pan the result: on an iGPU that is ~1 ms/frame cheaper
// than the zoom-aware default (hermes-*.glsl), whose dots keep
// their screen size but must re-render every frame the camera moves.
precision highp float;

varying vec2 v_coords;
uniform vec2 size;
uniform vec2 u_camera;

const vec3 PAPER = vec3(0.973, 0.976, 0.996);       // --ui-bg-chrome   #f8f9fe
const vec3 PAPER_TINT = vec3(0.953, 0.969, 1.0);    // --ui-bg-sidebar  #f3f7ff
const vec3 DOT_MINOR = vec3(0.765, 0.820, 0.941);   // --ui-stroke-secondary #c3d1f0
const vec3 DOT_MAJOR = vec3(0.580, 0.675, 0.890);   // stroke-primary, deeper #94ace3
const vec3 NOUS_BLUE = vec3(0.0, 0.325, 0.992);     // --dt-primary     #0053fd

const float MINOR_SPACING = 40.0;   // canvas px
const float MAJOR_EVERY = 5.0;      // a major dot every N minor dots
const float DOT_RADIUS_PX = 1.15;   // canvas px
const float MIN_SCREEN_GAP = 11.0;  // fade a grid level out below this gap
const float GRAIN = 0.010;          // paper grain amplitude
const float CLOUD_SCALE = 900.0;    // canvas px per tint wave radian
const float HOME_RADIUS_PX = 9.0;   // canvas px

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Coverage of a dot lattice with `spacing` (canvas px); the camera is reduced
// modulo the period first so large canvas coordinates keep float precision.
float dots(vec2 local, float spacing, float zoom) {
    vec2 p = local + mod(u_camera, spacing);
    vec2 g = mod(p, spacing);
    vec2 d = min(g, spacing - g);
    float dist_px = length(d) * zoom;
    float cover = 1.0 - smoothstep(DOT_RADIUS_PX - 0.6, DOT_RADIUS_PX + 0.6, dist_px);
    float gap_px = spacing * zoom;
    return cover * smoothstep(MIN_SCREEN_GAP * 0.6, MIN_SCREEN_GAP, gap_px);
}

void main() {
    // Features are sized in canvas px and scale with the zoom like windows do.
    float zoom = 1.0;
    vec2 local = v_coords * size;              // canvas px from the viewport corner
    vec2 canvas = local + u_camera;            // absolute canvas px

    // A broad, cheap tint wave instead of value noise: the background runs
    // every frame while the camera moves, so it has to stay a few ALU ops.
    vec2 wave_p = (local + mod(u_camera, CLOUD_SCALE * 6.2831853)) / CLOUD_SCALE;
    float wave = sin(wave_p.x) * sin(wave_p.y * 0.8 + 1.3);
    vec3 col = mix(PAPER, PAPER_TINT, smoothstep(-0.2, 0.9, wave) * 0.8);

    vec2 grain_p = floor((local + mod(u_camera, 4096.0)) * zoom);
    col += (hash(grain_p) - 0.5) * GRAIN;

    float major_spacing = MINOR_SPACING * MAJOR_EVERY;
    float minor = dots(local, MINOR_SPACING, zoom);
    float major = dots(local, major_spacing, zoom);
    float coarse = dots(local, major_spacing * MAJOR_EVERY, zoom);
    col = mix(col, DOT_MINOR, minor);
    col = mix(col, DOT_MAJOR, max(major, coarse));

    float r_px = length(canvas) * zoom;
    float ring = 1.0 - smoothstep(0.6, 1.4, abs(r_px - HOME_RADIUS_PX));
    float core = 1.0 - smoothstep(1.6, 2.6, r_px);
    col = mix(col, NOUS_BLUE, max(ring * 0.55, core * 0.85));

    gl_FragColor = vec4(col, 1.0);
}
