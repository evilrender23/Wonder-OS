#version 100
// Fragment stage of the close effects: a closed window's picture leaving the
// screen in one of several ways, after the Burn My Windows extension. Drawn
// over a plain quad that overhangs the picture by `pad` texels, with the warp
// renderer's vertex stage. Everything is premultiplied.

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform sampler2D tex;
uniform float alpha;
// Damage rectangle in frame pixels, (x0, y0, x1, y1).
uniform vec4 clip;
// Texels the picture occupies, and texels the texture was allocated with.
uniform vec2 used;
uniform vec2 alloc;
// Which effect (the order of CloseEffect::ALL), how far along it is (0-1),
// two random numbers of this close, and the overhang in texels.
uniform float effect;
uniform float progress;
uniform vec2 seed;
uniform float pad;

varying vec2 v_uv;
varying vec2 v_pos;

float hash(vec2 p) {
    p = fract(p * vec2(0.1031, 0.1030) + seed * 0.37);
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; i++) {
        sum += amp * noise(p);
        p = p * 2.03 + 17.0;
        amp *= 0.5;
    }
    return sum / 0.9375;
}

// The picture at `uv` (0-1 across it), nothing outside, antialiased border.
vec4 pic(vec2 uv) {
    vec2 texel = uv * used;
    vec2 edge = clamp(min(texel, used - texel) + 0.5, 0.0, 1.0);
    vec2 at = clamp(texel, vec2(0.5), used - vec2(0.5)) / alloc;
    return texture2D(tex, at) * (edge.x * edge.y);
}

// 1 inside the picture, falling to 0 across the overhang.
float reach(vec2 uv) {
    vec2 texel = uv * used;
    vec2 d = max(max(-texel, texel - used), vec2(0.0));
    return clamp(1.0 - length(d) / max(pad, 1.0), 0.0, 1.0);
}

vec3 flame(float heat) {
    vec3 c = mix(vec3(0.35, 0.02, 0.0), vec3(1.0, 0.35, 0.02), smoothstep(0.0, 0.45, heat));
    c = mix(c, vec3(1.0, 0.8, 0.25), smoothstep(0.4, 0.8, heat));
    return mix(c, vec3(1.0, 0.97, 0.85), smoothstep(0.8, 1.0, heat));
}

// A burning front: `d` is how far this pixel is ahead of it (negative once
// burnt), in units where the flames reach about 0.2 ahead.
vec4 burn(vec2 uv, float d, float flicker) {
    vec4 color = pic(uv) * smoothstep(-0.01, 0.015, d);
    // Scorched just ahead of the front.
    color.rgb *= mix(0.15, 1.0, smoothstep(0.0, 0.12, d));
    float ahead = 1.0 - d / (0.06 + 0.3 * flicker * flicker);
    float behind = 1.0 + d / 0.04;
    float heat = clamp(d > 0.0 ? ahead : behind, 0.0, 1.0);
    heat *= (0.6 + 0.4 * flicker) * reach(uv);
    float glow = heat * heat * (3.0 - 2.0 * heat);
    // Light, not paint: the flames add to what is behind them and only
    // their core covers it.
    color = color * (1.0 - 0.7 * glow * glow) + vec4(flame(heat) * glow * 1.25, glow * glow * 0.7);
    return color;
}

vec4 fx_fade(vec2 uv, float t) {
    return pic(uv) * (1.0 - t * t);
}

vec4 fx_fire(vec2 uv, float t) {
    vec2 px = uv * used;
    float wobble = fbm(vec2(px.x * 0.012, px.y * 0.012 + t * 5.0)) - 0.5;
    // Tongues: fine across, long upward, streaming up.
    float flicker = fbm(vec2(px.x * 0.045, px.y * 0.009 + t * 9.0));
    float front = t * 1.7 - 0.4 + wobble * 0.22;
    return burn(uv, (1.0 - uv.y) - front, flicker);
}

vec4 fx_incinerate(vec2 uv, float t) {
    vec2 px = uv * used;
    // Somewhere on the border, picked by the seed.
    float along = seed.x * 4.0;
    float side = floor(along);
    float f = fract(along);
    vec2 origin = side < 1.0 ? vec2(f, 0.0) : side < 2.0 ? vec2(1.0, f)
        : side < 3.0 ? vec2(1.0 - f, 1.0) : vec2(0.0, 1.0 - f);
    float span = max(max(length(origin * used), length((origin - vec2(1.0, 0.0)) * used)),
                    max(length((origin - vec2(0.0, 1.0)) * used), length((origin - 1.0) * used)));
    float dist = length((uv - origin) * used) / span;
    float wobble = fbm(px * 0.01 + seed * 50.0) - 0.5;
    float flicker = fbm(px * 0.03 + vec2(0.0, t * 12.0));
    float front = t * 1.75 - 0.45 + wobble * 0.35;
    return burn(uv, dist - front, flicker);
}

vec4 fx_tv(vec2 uv, float t) {
    float a = smoothstep(0.0, 0.6, t);
    float b = smoothstep(0.55, 0.95, t);
    float sy = max(mix(1.0, 0.0, a * a * (3.0 - 2.0 * a)), 3.0 / used.y);
    float sx = max(1.0 - b, 3.0 / used.x);
    vec2 q = (uv - 0.5) / vec2(sx, sy) + 0.5;
    vec4 color = pic(q);
    // The phosphor overdrives as the picture collapses.
    color = mix(color, vec4(color.a), clamp(a * 1.2, 0.0, 1.0));
    return color * (1.0 - smoothstep(0.9, 1.0, t));
}

vec4 fx_glide(vec2 uv, float t) {
    float e = t * t;
    float s = 1.0 - 0.3 * e;
    vec2 q = uv - 0.5;
    q.y = q.y / s + 0.12 * e;
    // Tilted back: the top edge recedes.
    q.x = q.x / (s * (1.0 + 0.6 * e * q.y));
    return pic(q + 0.5) * (1.0 - smoothstep(0.0, 1.0, t));
}

vec4 fx_pixelate(vec2 uv, float t) {
    vec2 px = uv * used;
    float block = 1.0 + t * 46.0;
    vec4 color = pic((floor(px / block) + 0.5) * block / used);
    float gone = step(hash(floor(px / 24.0)), (t - 0.15) / 0.8);
    return color * (1.0 - gone);
}

vec4 fx_hexagon(vec2 uv, float t) {
    vec2 p = uv * used / 46.0;
    vec2 r = vec2(1.0, 1.7320508);
    vec2 h = r * 0.5;
    vec2 a = mod(p, r) - h;
    vec2 b = mod(p - h, r) - h;
    vec2 gv = dot(a, a) < dot(b, b) ? a : b;
    vec2 id = p - gv;
    float lt = clamp(t * 1.7 - hash(id * 7.0) * 0.7, 0.0, 1.0);
    float dist = max(abs(gv.x), dot(abs(gv), vec2(0.5, 0.8660254))) * 2.0;
    float size = 1.05 * (1.0 - lt * lt);
    float inside = 1.0 - smoothstep(size - 0.04, size, dist);
    float rim = smoothstep(size - 0.22, size, dist) * inside;
    vec4 color = pic(uv);
    float lit = rim * min(lt * 6.0, 1.0) * color.a;
    color.rgb = color.rgb * (1.0 - lit) + vec3(0.45, 0.85, 1.0) * lit;
    return color * inside * (1.0 - lt * lt * lt);
}

vec4 fx_energize(vec2 uv, float t) {
    vec2 px = uv * used;
    float r = hash(vec2(floor(px.x / 3.0), 3.0));
    float lt = clamp(t * 1.9 - r * 0.9, 0.0, 1.0);
    // The column is drawn up and out through the top.
    float lift = lt * lt * 0.7;
    vec4 color = pic(vec2(uv.x, uv.y + lift * (0.4 + uv.y)));
    float shine = lt * (1.0 - lt) * 4.0;
    float sparkle = step(0.93, hash(floor(px / 3.0) + floor(t * 40.0))) * shine;
    color.rgb = mix(color.rgb, vec3(0.6, 0.85, 1.0) * color.a, clamp(shine * 0.8 + sparkle, 0.0, 1.0));
    return color * (1.0 - lt);
}

vec4 fx_matrix(vec2 uv, float t) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }
    vec2 px = uv * used;
    float cell = 14.0;
    float col = floor(px.x / cell);
    float r = hash(vec2(col, 11.0));
    float head = t * 2.1 - r * 0.75;
    float behind = head - uv.y;
    vec4 color = pic(uv) * step(behind, 0.0);
    if (behind > 0.0) {
        float trail = clamp(1.0 - behind / 0.4, 0.0, 1.0);
        vec2 slot = vec2(col, floor(px.y / cell));
        // A glyph is a 3x5 pattern of dots that changes now and then.
        float tick = floor(t * 18.0 + hash(slot) * 6.0);
        vec2 dot_ = floor(fract(px / cell) * vec2(4.0, 6.0));
        float on = step(0.45, hash(slot * 13.0 + dot_ * 3.0 + tick))
            * step(dot_.x, 2.5) * step(dot_.y, 4.5);
        float first = step(behind, cell / used.y);
        vec3 green = mix(vec3(0.05, 0.9, 0.3), vec3(0.8, 1.0, 0.85), first);
        float a = on * trail * (1.0 - smoothstep(0.85, 1.0, t));
        color = vec4(green * a, a) + vec4(0.0, 0.0, 0.0, 0.55 * trail * trail) * (1.0 - a);
    }
    return color;
}

vec4 fx_doom(vec2 uv, float t) {
    if (uv.y < 0.0 || uv.y > 1.0) {
        return vec4(0.0);
    }
    float col = floor(uv.x * used.x / 6.0);
    float r = noise(vec2(col * 0.35, 5.0));
    float drop = max(0.0, t * 1.6 - r * 0.6);
    return pic(vec2(uv.x, uv.y - drop * drop * 1.1 - drop * 0.1));
}

vec4 fx_apparition(vec2 uv, float t) {
    float e = t * t * (3.0 - 2.0 * t);
    vec2 q = (uv - 0.5) * used;
    float radius = length(q) / (0.5 * length(used));
    float angle = e * 5.0 * (1.0 - smoothstep(0.0, 1.2, radius));
    float c = cos(angle);
    float s = sin(angle);
    q = vec2(c * q.x - s * q.y, s * q.x + c * q.y) / (1.0 - 0.55 * e);
    return pic(q / used + 0.5) * (1.0 - e) * (1.0 - e * radius) * reach(uv);
}

void main() {
    if (v_pos.x < clip.x || v_pos.y < clip.y || v_pos.x >= clip.z || v_pos.y >= clip.w) {
        discard;
    }
    float t = clamp(progress, 0.0, 1.0);
    vec4 color;
    if (effect < 0.5) {
        color = fx_fade(v_uv, t);
    } else if (effect < 1.5) {
        color = fx_fire(v_uv, t);
    } else if (effect < 2.5) {
        color = fx_incinerate(v_uv, t);
    } else if (effect < 3.5) {
        color = fx_tv(v_uv, t);
    } else if (effect < 4.5) {
        color = fx_glide(v_uv, t);
    } else if (effect < 5.5) {
        color = fx_pixelate(v_uv, t);
    } else if (effect < 6.5) {
        color = fx_hexagon(v_uv, t);
    } else if (effect < 7.5) {
        color = fx_energize(v_uv, t);
    } else if (effect < 8.5) {
        color = fx_matrix(v_uv, t);
    } else if (effect < 9.5) {
        color = fx_doom(v_uv, t);
    } else {
        color = fx_apparition(v_uv, t);
    }
    // Every effect is gone by the end, whatever its own curve left behind.
    gl_FragColor = color * (alpha * (1.0 - smoothstep(0.94, 1.0, t)));
}
