// Threads — ported from React Bits <Threads /> (see ../LICENSE-REACTBITS.md).
// Single pass: the instanced line strips are evaluated per pixel — every
// thread's height at this column is computed analytically and composited in
// draw order (premultiplied "over"), then the screen pass curve is applied.
// The per-thread Perlin2D lookup shares one noise column, so its lattice rows
// are hoisted out of the thread loop (same values, far fewer hashes), and
// lattice cells whose threads cannot reach this pixel are skipped.
// Cursor steering/parting are dropped.

float Perlin2D(vec2 P) {
    vec2 Pi = floor(P);
    vec4 Pf_Pfmin1 = P.xyxy - vec4(Pi, Pi + 1.0);
    vec4 Pt = vec4(Pi.xy, Pi.xy + 1.0);
    Pt = Pt - floor(Pt * (1.0 / 71.0)) * 71.0;
    Pt += vec2(26.0, 161.0).xyxy;
    Pt *= Pt;
    Pt = Pt.xzxz * Pt.yyww;
    vec4 hash_x = fract(Pt * (1.0 / 951.135664));
    vec4 hash_y = fract(Pt * (1.0 / 642.949883));
    vec4 grad_x = hash_x - 0.49999;
    vec4 grad_y = hash_y - 0.49999;
    vec4 grad_results = inversesqrt(grad_x * grad_x + grad_y * grad_y) * (grad_x * Pf_Pfmin1.xzxz + grad_y * Pf_Pfmin1.yyww);
    grad_results *= 1.4142135623730950;
    vec2 blend = Pf_Pfmin1.xy * Pf_Pfmin1.xy * Pf_Pfmin1.xy * (Pf_Pfmin1.xy * (Pf_Pfmin1.xy * 6.0 - 15.0) + 10.0);
    vec4 blend2 = vec4(blend, vec2(1.0 - blend));
    return dot(grad_results, blend2.zxzx * blend2.wwyy);
}

// One lattice row of Perlin2D at a fixed x: value = r.x + r.y * (Y - row).
vec2 perlinRow(float ix, float fx, float bx, float row) {
    vec2 pt = vec2(ix, ix + 1.0);
    pt = pt - floor(pt * (1.0 / 71.0)) * 71.0 + 26.0;
    float py = row - floor(row * (1.0 / 71.0)) * 71.0 + 161.0;
    vec2 v = pt * pt * (py * py);
    vec2 gx = fract(v * (1.0 / 951.135664)) - 0.49999;
    vec2 gy = fract(v * (1.0 / 642.949883)) - 0.49999;
    vec2 s = inversesqrt(gx * gx + gy * gy) * 1.4142135623730950;
    vec2 w = vec2(1.0 - bx, bx);
    float a = dot(w, s * gx * vec2(fx, fx - 1.0));
    float b = dot(w, s * gy);
    return vec2(a, b);
}

float threadY(float perc, float x, float xw, float j, vec2 rowA, vec2 rowB, float n2, float mixX) {
    float splitPoint = P_SPLIT + perc * P_FRAY;
    float amplitudeNormal = smoothstep(splitPoint, max(0.7, splitPoint + 0.2), x);
    float fy = (xw + perc) * 2.5 - j;
    float by = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);
    float n1 = mix(rowA.x + rowA.y * fy, rowB.x + rowB.y * (fy - 1.0), by);
    float xnoise = mix(n1, n2, mixX);
    return 0.5 + (perc - 0.5) * P_DISTANCE + xnoise * 0.25 * P_AMPLITUDE * amplitudeNormal;
}

float smootherEdge(float edge, float x) {
    float t = clamp(x / edge, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    // state.time advances at the component's default speed 0.6.
    float time = wl_time() * 0.6;

    float ang = radians(P_ANGLE);
    float cs = cos(ang);
    float sn = sin(ang);
    vec2 frame = vec2(abs(res.x * cs) + abs(res.y * sn), abs(res.x * sn) + abs(res.y * cs));
    vec2 p = frag - 0.5 * res;
    vec2 local = vec2(p.x * cs + p.y * sn, -p.x * sn + p.y * cs);
    float x = local.x / frame.x + 0.5;
    float yNorm = local.y / frame.y + 0.5;

    float pixel = 1.0 / max(res.x, res.y);
    float count = float(P_LINE_COUNT);
    float timeScaled = time / 10.0;
    float seed = P_SEED * 13.37;
    // The second noise octave does not depend on the thread.
    float n2 = Perlin2D(vec2(timeScaled * 3.5 + seed, (x * P_WAVES + timeScaled) * 3.5)) / 1.5;
    float mixX = x * 0.3;

    float X0 = timeScaled * 2.5 + seed;
    float ix = floor(X0);
    float fx = X0 - ix;
    float bx = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);
    float xw = x * P_WAVES;
    float maxBand = (3.5 * P_THICKNESS + 10.0 * P_SOFTNESS) * pixel + 1.0 / frame.y;
    // Chord-to-curve slack for a group of six threads (bounded curvature).
    float margin = maxBand + 0.0125 * P_AMPLITUDE + 0.002;

    vec4 acc = vec4(0.0);
    if (x >= 0.0 && x <= 1.0) {
        float j0 = floor(xw * 2.5);
        vec2 rowA = perlinRow(ix, fx, bx, j0);
        for (int k = 0; k < 4; k++) {
            float j = j0 + float(k);
            vec2 rowB = perlinRow(ix, fx, bx, j + 1.0);
            float pl = j / 2.5 - xw;
            float ph = (j + 1.0) / 2.5 - xw;
            int i0 = int(max(ceil(count * pl - 1e-4), 0.0));
            int i1 = int(min(ceil(count * ph - 1e-4), count));
            if (i1 > i0) {
                float plc = float(i0) / count;
                float phc = float(i1 - 1) / count;
                float nb = max(abs(rowA.x) + abs(rowA.y), abs(rowB.x) + abs(rowB.y));
                float xb = (1.0 - mixX) * nb + mixX * abs(n2);
                float spl = P_SPLIT + plc * P_FRAY;
                float aMax = smoothstep(spl, max(0.7, spl + 0.2), x) * 0.25 * P_AMPLITUDE * xb;
                float lo = 0.5 + (min(plc, phc) - 0.5) * P_DISTANCE - aMax - maxBand;
                float hi = 0.5 + (max(plc, phc) - 0.5) * P_DISTANCE + aMax + maxBand;
                if (yNorm >= lo && yNorm <= hi) {
                    for (int g = 0; g < 14; g++) {
                        int ga = i0 + g * 6;
                        if (ga >= i1) break;
                        int gb = ga + 5;
                        if (gb > i1 - 1) gb = i1 - 1;
                        // Threads in a group of six stay close: test the
                        // chord between the first and last one first.
                        float yA = threadY(float(ga) / count, x, xw, j, rowA, rowB, n2, mixX);
                        float yB = threadY(float(gb) / count, x, xw, j, rowA, rowB, n2, mixX);
                        if (yNorm < min(yA, yB) - margin || yNorm > max(yA, yB) + margin) continue;
                        for (int ii = 0; ii < 6; ii++) {
                            int i = ga + ii;
                            if (i > gb) break;
                            float perc = float(i) / count;
                            float y = threadY(perc, x, xw, j, rowA, rowB, n2, mixX);
                            if (abs(yNorm - y) >= maxBand) continue;
                            float splitPoint = P_SPLIT + perc * P_FRAY;
                            float blur = smoothstep(splitPoint, splitPoint + 0.05, x) * perc;
                            float width = 7.0 * P_THICKNESS * pixel * (1.0 - perc);
                            float band = (width / 2.0 + 10.0 * P_SOFTNESS * pixel * blur) * frame.y;
                            float draw = max(band, 1.0);
                            float edge = (yNorm - y) * frame.y;
                            if (abs(edge) >= draw) continue;
                            float alpha = 1.0 - smoothstep(0.0, 1.0, pow(perc, 0.3)) * P_TAPER;
                            float coverage = (1.0 - smoothstep(0.0, draw, abs(edge))) * (band / draw) * alpha;
                            vec3 col = mix(P_COLOR, P_ACCENT_COLOR, perc);
                            acc = vec4(col * coverage, coverage) + acc * (1.0 - coverage);
                        }
                    }
                }
            }
            rowA = rowB;
        }
    }

    float a = acc.a;
    vec3 color = a > 0.0001 ? acc.rgb / a : vec3(0.0);
    float mask = 1.0;
    if (P_FADE > 0.0) {
        float e = P_FADE * min(res.x, res.y) * 0.5;
        mask *= smootherEdge(e, frag.x) * smootherEdge(e, res.x - frag.x);
        mask *= smootherEdge(e, frag.y) * smootherEdge(e, res.y - frag.y);
    }
    vec4 premul = vec4(color * pow(a, 2.0 / P_BRIGHTNESS), a) * mask;
    gl_FragColor = vec4(wl_over(P_BACKGROUND, premul), 1.0);
}
