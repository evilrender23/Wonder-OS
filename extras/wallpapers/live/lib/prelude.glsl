// WonderlandWM live wallpaper prelude. Copyright the WonderlandWM authors,
// GPL-3.0-or-later. `wonderland-wallpaper render` puts it after the
// `#define P_*` / `WL_*` lines and before the wallpaper body.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_coords;
// `size`, or `u_output_size` when the wallpaper is blended with a picture.
uniform vec2 WL_VIEWPORT;
uniform vec2 u_camera;
uniform float u_zoom;
uniform float u_time;

// Output size in screen pixels. driftwm hands background shaders the
// viewport in canvas units (output / zoom); undo that unless the wallpaper
// should zoom with the canvas.
vec2 wl_res() {
#if P_ZOOM_FOLLOW
    return WL_VIEWPORT;
#else
    return WL_VIEWPORT * u_zoom;
#endif
}

// Pixel position with a bottom-left origin, like WebGL's gl_FragCoord.xy,
// shifted by the canvas camera when the parallax setting is above 0.
vec2 wl_frag() {
    vec2 p = vec2(v_coords.x, 1.0 - v_coords.y) * wl_res();
#if P_ZOOM_FOLLOW
    vec2 cam = u_camera;
#else
    vec2 cam = u_camera * u_zoom;
#endif
    // Wrap the camera so huge canvas offsets don't eat float precision.
    cam = mod(cam * P_PARALLAX, 8192.0);
    return p + vec2(cam.x, -cam.y);
}

// Seconds, already scaled by the speed setting. Wraps every hour so the
// value keeps full precision however long the session runs.
float wl_time() {
#if WL_ANIMATED
    return mod(u_time, 3600.0) * P_SPEED + 4.0;
#else
    return 4.0;
#endif
}

// Helpers most ports need (GLSL ES 1.00 has none of these built in).
float wl_hash12(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

vec2 wl_hash22(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz) * q.zy);
}

float wl_tanh(float x) {
    float e = exp(2.0 * clamp(x, -10.0, 10.0));
    return (e - 1.0) / (e + 1.0);
}

float wl_smoother(float x) {
    x = clamp(x, 0.0, 1.0);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

// sRGB <-> linear, for components that blend in linear light.
vec3 wl_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 wl_to_srgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// Composite a premultiplied colour over an opaque background.
vec3 wl_over(vec3 background, vec4 premultiplied) {
    return background * (1.0 - clamp(premultiplied.a, 0.0, 1.0)) + premultiplied.rgb;
}
