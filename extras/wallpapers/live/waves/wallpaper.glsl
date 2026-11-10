// Waves — ported from React Bits <Waves /> (see ../LICENSE-REACTBITS.md).
// Single pass: the instanced line strips are found per pixel by inverting the
// Perlin wave warp with a few Newton steps, then the nearest lines are drawn
// from the linearised warp; where lines pile up into folds their coverage is
// averaged. The cursor/ripple spring field and the intro are dropped.

const float VAR = float(P_VARIATION);

vec2 grad_of(vec2 cell) {
    float h = wl_hash12(cell + vec2(VAR * 7.919, VAR * 10.4729) + 0.5);
    float i = floor(h * 12.0);
    if (i < 1.0) return vec2(1.0, 1.0);
    if (i < 2.0) return vec2(-1.0, 1.0);
    if (i < 3.0) return vec2(1.0, -1.0);
    if (i < 4.0) return vec2(-1.0, -1.0);
    if (i < 5.0 || (i >= 6.0 && i < 7.0)) return vec2(1.0, 0.0);
    if (i < 8.0) return vec2(-1.0, 0.0);
    if (i < 9.0 || (i >= 10.0 && i < 11.0)) return vec2(0.0, 1.0);
    return vec2(0.0, -1.0);
}

// Perlin noise (quintic fade) with its analytic gradient: (value, d/dx, d/dy).
vec3 perlin(vec2 p) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 ga = grad_of(i);
    vec2 gb = grad_of(i + vec2(1.0, 0.0));
    vec2 gc = grad_of(i + vec2(0.0, 1.0));
    vec2 gd = grad_of(i + vec2(1.0, 1.0));
    float va = dot(ga, f);
    float vb = dot(gb, f - vec2(1.0, 0.0));
    float vc = dot(gc, f - vec2(0.0, 1.0));
    float vd = dot(gd, f - vec2(1.0, 1.0));
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float k = va - vb - vc + vd;
    float value = va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * k;
    vec2 g = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd)
           + du * (vec2(vb - va, vc - va) + u.yx * k);
    return vec3(value, g);
}

// Newton solve of rest + D(rest) = target from `start`, where
// D = amp * (cos th, 0.5 sin th), th = perlin(rest * freq + drift) * tw.
// Returns (rest.x, rest.y, pixels per unit of u across the lines, converged).
vec4 solve(vec2 start, vec2 target, vec2 freq, vec2 drift, float amp, float tw, out float fold_risk) {
    vec2 rest = start;
    vec2 a = vec2(0.0);
    vec2 g = vec2(0.0);
    float last = 1e3;
    for (int n = 0; n < 4; n++) {
        vec3 pn = perlin(rest * freq + drift);
        float th = pn.x * tw;
        g = pn.yz * freq * tw;
        a = amp * vec2(-sin(th), 0.5 * cos(th));
        vec2 G = rest + amp * vec2(cos(th), 0.5 * sin(th)) - target;
        float det = 1.0 + dot(a, g);
        det = sign(det) * max(abs(det), 0.15);
        vec2 step_r = G - a * dot(g, G) / det;
        float l = length(step_r);
        if (l > amp * 0.75 + 1.0) step_r *= (amp * 0.75 + 1.0) / l;
        last = length(G);
        rest -= step_r;
    }
    float det = abs(1.0 + dot(a, g));
    vec2 fv = vec2(a.x * g.y, 1.0 + a.y * g.y);
    // Fold risk: how close the warp is to folding here (|dD/drest|).
    fold_risk = length(a) * length(g);
    return vec4(rest, det / max(length(fv), 1e-4), last < 0.75 ? 1.0 : 0.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float flow = wl_time() + VAR * 37.1;
    vec2 freq = vec2(0.002, 0.0015) / P_SCALE;
    vec2 drift = vec2(flow * 0.025, flow * 0.0075);
    float amp = P_AMPLITUDE;
    float tw = 12.0 * P_TWIST;
    float rot = radians(P_ROTATION);
    vec2 axis = vec2(cos(rot), sin(rot));

    // Screen (y down, like the component) -> unrotated wave space.
    vec2 screen = vec2(frag.x, res.y - frag.y) - 0.5 * res;
    vec2 target = vec2(screen.x * axis.x + screen.y * axis.y, -screen.x * axis.y + screen.y * axis.x);

    float width = P_LINE_WIDTH;
    float half_w = max(0.5, width * 0.5);
    float thin = min(1.0, width);
    float base_alpha = 0.62;
    float period = P_SPACING * 2.6;
    float flow_off = mod(flow * 16.0, period);

    // Up to three sheets of the warp can overlap where the lines fold; solve
    // from both sides as well as straight on and draw each distinct sheet.
    float keep = 1.0;
    float found_a = -1e9;
    float found_b = -1e9;
    float risk = 0.0;
    for (int sheet = 0; sheet < 3; sheet++) {
        // Other sheets can only overlap near a fold.
        if (sheet > 0 && risk < 0.45) break;
        float side = sheet == 0 ? 0.0 : (sheet == 1 ? -1.0 : 1.0);
        float r_here;
        vec4 sol = solve(target + vec2(side * amp, 0.0), target, freq, drift, amp, tw, r_here);
        if (sheet == 0) risk = r_here;
        if (sheet == 0 && sol.w < 0.5) risk = 1.0;
        if (sheet > 0 && sol.w < 0.5) continue;
        if (abs(sol.x - found_a) < P_SPACING * 0.5 || abs(sol.x - found_b) < P_SPACING * 0.5) continue;
        if (sheet == 0) found_a = sol.x; else found_b = sol.x;
        vec2 rest = sol.xy;
        float px_per_u = sol.z;
        float sheet_keep = 1.0;
        float k0 = floor(rest.x / P_SPACING + 0.5);
        for (int j = -3; j <= 3; j++) {
            float k = k0 + float(j);
            float across = abs(k * P_SPACING - rest.x) * px_per_u;
            float cov = clamp(half_w + 0.5 - across, 0.0, 1.0) * thin;
#if P_LINE_STYLE == 1
            float ph = wl_hash12(vec2(k * 0.7381 + 3.1, VAR * 1.37 + 11.0));
            float phase = (rest.y + flow_off) / period + ph;
            float blur = max(1.0 / period, 0.0001);
            cov *= clamp((0.25 - abs(fract(phase) - 0.5)) / blur + 0.5, 0.0, 1.0);
#endif
            sheet_keep *= 1.0 - cov * base_alpha;
        }
        // Folds: lines closer than a pixel or two blend into their average.
        float spacing_px = P_SPACING * px_per_u;
        float layers = 2.0 * half_w / max(spacing_px, 1e-3);
#if P_LINE_STYLE == 1
        layers *= 0.5;
#endif
        float avg_keep = pow(max(1.0 - thin * base_alpha, 0.0), min(layers, 40.0));
        sheet_keep = mix(sheet_keep, min(sheet_keep, avg_keep), smoothstep(2.5, 1.0, spacing_px) * sol.w);
        keep *= sheet_keep;
    }
    float alpha = 1.0 - keep;

    vec2 uv = frag / res;
    float edge = P_FADE * 0.5;
    float mask = wl_smoother(uv.x / max(edge, 0.0001)) * wl_smoother((1.0 - uv.x) / max(edge, 0.0001))
               * wl_smoother(uv.y / max(edge, 0.0001)) * wl_smoother((1.0 - uv.y) / max(edge, 0.0001));
    alpha *= mask;

    vec3 color = P_LINE_COLOR;
    vec3 back = P_BACKGROUND;
#if P_LIGHT_MODE
    vec3 lin = pow(color, vec3(2.2));
    float tone = dot(lin, vec3(0.2126, 0.7152, 0.0722));
    float depth = min(1.0, 0.045 / max(tone, 0.0001));
    color = pow(lin * depth, vec3(1.0 / 2.2));
    // Ink is meant for light pages: keep a dark background readable.
    if (dot(back, vec3(0.2126, 0.7152, 0.0722)) < 0.5) back = vec3(0.965, 0.965, 0.97);
#endif
    gl_FragColor = vec4(wl_over(back, vec4(color * alpha, alpha)), 1.0);
}
