// WonderlandWM live wallpaper blend. Copyright the WonderlandWM authors,
// GPL-3.0-or-later. `wonderland-wallpaper render` appends it to a live
// wallpaper that is blended with one of the user's pictures («Mezclar con
// imagen»): the wallpaper's `main` is renamed to `wl_live_main`, and this
// `main` multiplies the picture over what it drew.
//
// `tex` is the picture, bound through `[background] texture`. It covers the
// screen and stays put: the animation moves, pans and zooms under it.
uniform sampler2D tex;
uniform vec2 u_texture_size;

void main() {
    wl_live_main();
    vec3 live = clamp(gl_FragColor.rgb, 0.0, 1.0);

    // Cover: only the viewport's aspect matters, so no zoom uniform.
    vec2 ratio = WL_VIEWPORT / u_texture_size;
    vec2 drawn = u_texture_size * max(ratio.x, ratio.y);
    vec2 uv = (v_coords - 0.5) * WL_VIEWPORT / drawn + 0.5;
    vec3 img = texture2D(tex, clamp(uv, 0.0, 1.0)).rgb;
    float luma = dot(img, vec3(0.2126, 0.7152, 0.0722));
    img = clamp(mix(vec3(luma), img, P_BLEND_SATURATION), 0.0, 1.0);

    // A plain multiply leaves a dark animation black: lifting its shadows
    // towards white lets the picture through there too.
    vec3 light = mix(live, vec3(1.0), P_BLEND_SHADOWS);
    vec3 mixed = light * img * P_BLEND_BRIGHTNESS;
    // Above 1 the brightness would clip the picture's highlights to flat
    // white: ease them into it instead.
    vec3 over = max(mixed - 0.75, 0.0);
    mixed = min(mixed, 0.75) + 0.25 * (1.0 - exp(-over * 4.0));
    gl_FragColor = vec4(clamp(mix(live, mixed, P_BLEND_AMOUNT), 0.0, 1.0), 1.0);
}
