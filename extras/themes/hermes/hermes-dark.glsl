// Hermes Dark — canvas background for WonderlandWM.
// Palette from Hermes Agent Desktop's default "nous" theme (dark mode):
// background #0d1117, sidebar #010409, border #30363d, Nous blue #4a84fe.
// A paper-grain canvas with a two-level dot grid whose dots keep the same
// screen size at any zoom, plus a small "home" mark at the canvas origin.
precision highp float;

varying vec2 v_coords;
uniform vec2 size;
uniform vec2 u_camera;
uniform float u_zoom;

const vec3 PAPER = vec3(0.051, 0.067, 0.090);       // background       #0d1117
const vec3 PAPER_TINT = vec3(0.059, 0.086, 0.137);  // primary tint     #0f1623
const vec3 DOT_MINOR = vec3(0.176, 0.204, 0.251);   // muted+border     #2d3440
const vec3 DOT_MAJOR = vec3(0.243, 0.329, 0.502);   // primary on bg    #3e5480
const vec3 NOUS_BLUE = vec3(0.290, 0.518, 0.996);   // primary (dark)   #4a84fe

const float MINOR_SPACING = 40.0;   // canvas px
const float MAJOR_EVERY = 5.0;      // a major dot every N minor dots
const float DOT_RADIUS_PX = 1.15;   // screen px, constant across zoom
const float MIN_SCREEN_GAP = 11.0;  // fade a grid level out below this gap
const float GRAIN = 0.012;          // paper grain amplitude
const float CLOUD_SCALE = 900.0;    // canvas px per tint wave radian
const float HOME_RADIUS_PX = 9.0;

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
    float zoom = max(u_zoom, 1e-4);
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
