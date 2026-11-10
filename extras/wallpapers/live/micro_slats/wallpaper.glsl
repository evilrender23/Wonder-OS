// MicroSlats — ported from React Bits <MicroSlats /> (see ../LICENSE-REACTBITS.md).
// The per-slat field pass is evaluated inline for the slat under the pixel
// (one cell, as in the original when the fluid is idle). The cursor fluid
// (stirring, ink trail, splashes, lean) and the intro are dropped.

float wave_turn(int i)   { if (i == 0) return 0.0; if (i == 1) return -0.95; if (i == 2) return 0.78; return 1.62; }
float wave_length(int i) { if (i == 0) return 2.6; if (i == 1) return 1.7;  if (i == 2) return 1.12; return 0.76; }
float wave_height(int i) { if (i == 0) return 1.0; if (i == 1) return 0.75; if (i == 2) return 0.5;  return 0.3; }
float wave_shift(int i)  { if (i == 0) return 0.0; if (i == 1) return 2.3;  if (i == 2) return 4.1;  return 1.1; }

// Preset table: scale, speed, direction, chop, stretch, glint, contrast,
// perspective, fog. «swell» uses the settings (its defaults are the preset).
void preset(out float scale, out float speed, out float direction, out float chop,
            out float stretch, out float glint, out float contrast,
            out float perspective, out float fog) {
#if P_PRESET == 1
    scale = 1.3; speed = 0.5; direction = 262.0; chop = 0.2; stretch = 0.12;
    glint = 0.45; contrast = 1.1; perspective = 0.5; fog = 0.4;
#elif P_PRESET == 2
    scale = 0.55; speed = 1.6; direction = 236.0; chop = 1.0; stretch = 0.3;
    glint = 1.3; contrast = 1.8; perspective = 0.8; fog = 0.35;
#elif P_PRESET == 3
    scale = 0.85; speed = 1.2; direction = 180.0; chop = 0.6; stretch = 0.85;
    glint = 0.25; contrast = 1.2; perspective = 0.0; fog = 0.0;
#else
    scale = P_SCALE; speed = P_WAVE_SPEED; direction = P_DIRECTION; chop = P_CHOP;
    stretch = P_STRETCH; glint = P_GLINT; contrast = P_CONTRAST;
    perspective = P_PERSPECTIVE; fog = P_FOG;
#endif
}

float pill(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float W = res.x;
    float H = res.y;

    float scale, speed, direction_deg, chop, stretch, glint_k, contrast, perspective, fog;
    preset(scale, speed, direction_deg, chop, stretch, glint_k, contrast, perspective, fog);
    chop = clamp(chop, 0.0, 1.5);
    stretch = clamp(stretch, 0.0, 0.95);
    contrast = clamp(contrast, 0.2, 4.0);
    perspective = clamp(perspective, 0.0, 1.0);
    fog = clamp(fog, 0.0, 1.0);
    float time = wl_time() * speed;
    float direction = direction_deg * 0.017453292;

    float slatW = clamp(P_SLAT_WIDTH, 1.0, 64.0);
    float slatH = clamp(P_SLAT_HEIGHT, 2.0, 240.0);
    float gapSize = clamp(P_GAP, 0.0, 64.0);
    vec2 pitch = vec2(slatW + gapSize, slatH + gapSize);
    vec2 slat = vec2(slatW, slatH);
    float cols = ceil((W + gapSize) / pitch.x) + 1.0;
    float rows = ceil((H + gapSize) / pitch.y) + 1.0;
    vec2 origin = vec2((W - (cols * pitch.x - gapSize)) * 0.5, (H - (rows * pitch.y - gapSize)) * 0.5);

    // Top-left origin, like the component's CSS-pixel layout.
    vec2 p = vec2(frag.x, H - frag.y);
    vec2 local = p - origin;
    vec2 cell = floor(local / pitch);
    vec2 center = origin + cell * pitch + slat * 0.5;

    // ---- field pass for this slat ----
    float v = clamp(center.y / H, 0.0, 1.0);
    float horizon = mix(8.0, 0.5, perspective);
    float depth = (1.0 + horizon) / (v + horizon);
    vec2 world = vec2((center.x - W * 0.5) / H * depth, (depth - 1.0) * horizon * 2.2);
    world /= max(scale, 0.05);

    float meander = 0.55 * sin(dot(world, vec2(0.23, 0.41)) * 0.9 + time * 0.13)
        + 0.3 * sin(dot(world, vec2(-0.37, 0.19)) * 1.4 - time * 0.09);

    float steep = chop * 0.45;
    float height = 0.0;
    float total = 0.0;
    for (int i = 0; i < 4; i++) {
        float angle = direction + wave_turn(i);
        vec2 heading = vec2(cos(angle), sin(angle));
        float k = 6.2831853 / wave_length(i);
        float omega = sqrt(9.81 * k) * 0.35;
        float theta = k * dot(world, heading) - omega * time + wave_shift(i) + meander * (0.6 + 0.3 * float(i));
        height += wave_height(i) * (cos(theta) + steep * cos(2.0 * theta));
        total += wave_height(i) * (1.0 + steep);
    }
    float level = clamp(0.5 + 0.5 * height / (total * 0.85), 0.0, 1.0);
    float crest = smoothstep(0.6, 1.0, level);
    float glint = crest * crest * glint_k;

    float haze = mix(1.0, 1.0 - fog, pow(1.0 - v, 1.4));
    float light = (pow(level, contrast) + glint * 0.8) * haze;
    light = clamp(light, 0.0, 1.0);
    light = max(light, 0.045);
    float span = clamp(mix(1.0 - stretch, 1.0, level), 0.08, 1.0);
    float tint = clamp(glint * 1.4, 0.0, 1.0);

    // ---- slat pass ----
    vec2 offset = local - cell * pitch - slat * 0.5;
    vec2 halfSize = vec2(slat.x * 0.5, slat.y * 0.5 * span);
    float radius = clamp(P_ROUNDNESS, 0.0, 1.0) * min(halfSize.x, halfSize.y);
    float alpha = clamp(0.5 - pill(offset, halfSize, radius), 0.0, 1.0) * light;
    vec3 col = mix(P_COLOR, P_GLINT_COLOR, tint) * alpha + P_BACKGROUND * (1.0 - alpha);
    gl_FragColor = vec4(col, 1.0);
}
