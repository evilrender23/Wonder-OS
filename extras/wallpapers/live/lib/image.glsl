// WonderlandWM image wallpaper. Copyright the WonderlandWM authors,
// GPL-3.0-or-later. `wonderland-wallpaper render img-…` puts the `#define P_*`
// lines before it and drops the uniform declarations the settings don't use
// (driftwm redraws on camera or zoom changes only while they are declared).
//
// It is a driftwm texture shader: `tex` is the imported picture, bound through
// `[background] texture = …`. `u_output_size` is the viewport in canvas units
// (output / zoom), so `u_output_size * u_zoom` is the output in screen pixels.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_coords;
uniform sampler2D tex;
uniform vec2 u_texture_size;
uniform vec2 u_output_size;
uniform vec2 u_camera;
uniform float u_zoom;

// P_FIT
#define FIT_COVER 0
#define FIT_CONTAIN 1
#define FIT_STRETCH 2
#define FIT_CENTER 3
#define FIT_TILE 4

void main() {
#if WL_USES_ZOOM
    float zoom = u_zoom;
#else
    float zoom = 1.0;
#endif
    // The output in screen pixels. Without the zoom uniform only its aspect is
    // right, which is all the fits that scale the picture to the screen need.
    vec2 screen = u_output_size * zoom;

    // Pixel position from the centre of the screen.
#if P_ZOOM_FOLLOW
    vec2 px = (v_coords - 0.5) * u_output_size;
#else
    vec2 px = (v_coords - 0.5) * screen;
#endif
#if WL_USES_CAMERA
    // Anchor the picture to the canvas: all the way at parallax 1.
    vec2 centre = u_camera + 0.5 * u_output_size;
#if !P_ZOOM_FOLLOW
    centre *= zoom;
#endif
    px += centre * P_PARALLAX;
#endif

    // Size the picture is drawn at, in screen pixels.
    vec2 ratio = screen / u_texture_size;
#if P_FIT == FIT_COVER
    vec2 drawn = u_texture_size * max(ratio.x, ratio.y);
#elif P_FIT == FIT_CONTAIN
    vec2 drawn = u_texture_size * min(ratio.x, ratio.y);
#elif P_FIT == FIT_STRETCH
    vec2 drawn = screen;
#else
    vec2 drawn = u_texture_size;
#endif
    drawn *= P_SCALE;

    // The framing settings say which part shows when the picture is larger
    // than the screen, and where it sits when it is smaller.
    vec2 uv = (px + 0.5 * screen - (screen - drawn) * vec2(P_FOCUS_X, P_FOCUS_Y)) / drawn;

#if P_FIT == FIT_TILE
    vec3 col = texture2D(tex, fract(uv)).rgb;
#elif P_FIT == FIT_CONTAIN || P_FIT == FIT_CENTER
    vec2 inside = step(vec2(0.0), uv) * step(uv, vec2(1.0));
    vec3 col = mix(P_BACKGROUND, texture2D(tex, clamp(uv, 0.0, 1.0)).rgb, inside.x * inside.y);
#else
    // Past its edges the picture meets its own reflection instead of a seam.
    vec3 col = texture2D(tex, 1.0 - abs(mod(uv, 2.0) - 1.0)).rgb;
#endif

    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(luma), col, P_SATURATION) * P_BRIGHTNESS;
    vec2 edge = v_coords - 0.5;
    col *= 1.0 - P_VIGNETTE * smoothstep(0.25, 0.75, length(edge) * 1.1);

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
