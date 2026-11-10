// LetterGlitch — ported from React Bits <LetterGlitch /> (see ../LICENSE-REACTBITS.md).
// Single pass: the canvas font atlas becomes a built-in 5x7 dot font with the
// component's 59 default characters, and the CPU glitch simulation (random
// ignitions, row streaks, hold/fade envelopes, glyph scrambling) becomes
// hashed per-cell events in 2-second time slots, evaluated analytically for
// the cell and the 7 cells to its left. Cursor, click tears/bursts and the
// intro are dropped.

// Light mode: turn light ink into dark ink (white -> near black), keeping hue.
vec3 ink(vec3 c) {
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c * (1.0 - 0.85 * l * l);
}

// 59 glyphs, 5x7, in the component's default character order:
// ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$&*()-_+=/[]{};:<>.,0123456789
vec2 glyphBits(float g) {
    if (g < 29.0) {
        if (g < 14.0) {
            if (g < 7.0) {
                if (g < 3.0) {
                    if (g < 1.0) {
                        return vec2(1033774.0, 17969.0); // A
                    }
                    if (g < 2.0) {
                        return vec2(509487.0, 15921.0); // B
                    }
                    return vec2(34350.0, 14881.0); // C
                }
                if (g < 5.0) {
                    if (g < 4.0) {
                        return vec2(575023.0, 15921.0); // D
                    }
                    return vec2(492607.0, 31777.0); // E
                }
                if (g < 6.0) {
                    return vec2(492607.0, 1057.0); // F
                }
                return vec2(951854.0, 31281.0); // G
            }
            if (g < 10.0) {
                if (g < 8.0) {
                    return vec2(1033777.0, 17969.0); // H
                }
                if (g < 9.0) {
                    return vec2(135310.0, 14468.0); // I
                }
                return vec2(270620.0, 6440.0); // J
            }
            if (g < 12.0) {
                if (g < 11.0) {
                    return vec2(103729.0, 17701.0); // K
                }
                return vec2(33825.0, 31777.0); // L
            }
            if (g < 13.0) {
                return vec2(710513.0, 17969.0); // M
            }
            return vec2(708145.0, 17977.0); // N
        }
        if (g < 21.0) {
            if (g < 17.0) {
                if (g < 15.0) {
                    return vec2(575022.0, 14897.0); // O
                }
                if (g < 16.0) {
                    return vec2(509487.0, 1057.0); // P
                }
                return vec2(575022.0, 22837.0); // Q
            }
            if (g < 19.0) {
                if (g < 18.0) {
                    return vec2(509487.0, 17701.0); // R
                }
                return vec2(459838.0, 15888.0); // S
            }
            if (g < 20.0) {
                return vec2(135327.0, 4228.0); // T
            }
            return vec2(575025.0, 14897.0); // U
        }
        if (g < 25.0) {
            if (g < 23.0) {
                if (g < 22.0) {
                    return vec2(575025.0, 4433.0); // V
                }
                return vec2(706097.0, 10933.0); // W
            }
            if (g < 24.0) {
                return vec2(141873.0, 17962.0); // X
            }
            return vec2(141873.0, 4228.0); // Y
        }
        if (g < 27.0) {
            if (g < 26.0) {
                return vec2(139807.0, 31778.0); // Z
            }
            return vec2(135300.0, 4100.0); // !
        }
        if (g < 28.0) {
            return vec2(718382.0, 14397.0); // @
        }
        return vec2(359754.0, 10591.0); // #
    }
    if (g < 44.0) {
        if (g < 36.0) {
            if (g < 32.0) {
                if (g < 30.0) {
                    return vec2(464836.0, 4596.0); // $
                }
                if (g < 31.0) {
                    return vec2(70950.0, 22837.0); // &
                }
                return vec2(480384.0, 149.0); // *
            }
            if (g < 34.0) {
                if (g < 33.0) {
                    return vec2(67720.0, 8322.0); // (
                }
                return vec2(270466.0, 2184.0); // )
            }
            if (g < 35.0) {
                return vec2(1015808.0, 0.0); // -
            }
            return vec2(0.0, 31744.0); // _
        }
        if (g < 40.0) {
            if (g < 38.0) {
                if (g < 37.0) {
                    return vec2(1020032.0, 132.0); // +
                }
                return vec2(31744.0, 31.0); // =
            }
            if (g < 39.0) {
                return vec2(139776.0, 34.0); // /
            }
            return vec2(67662.0, 14402.0); // [
        }
        if (g < 42.0) {
            if (g < 41.0) {
                return vec2(270606.0, 14600.0); // ]
            }
            return vec2(69768.0, 8324.0); // {
        }
        if (g < 43.0) {
            return vec2(266370.0, 2180.0); // }
        }
        return vec2(6336.0, 2182.0); // ;
    }
    if (g < 51.0) {
        if (g < 47.0) {
            if (g < 45.0) {
                return vec2(6336.0, 198.0); // :
            }
            if (g < 46.0) {
                return vec2(34952.0, 8322.0); // <
            }
            return vec2(532610.0, 2184.0); // >
        }
        if (g < 49.0) {
            if (g < 48.0) {
                return vec2(0.0, 6336.0); // .
            }
            return vec2(0.0, 2182.0); // ,
        }
        if (g < 50.0) {
            return vec2(714286.0, 14899.0); // 0
        }
        return vec2(135364.0, 14468.0); // 1
    }
    if (g < 55.0) {
        if (g < 53.0) {
            if (g < 52.0) {
                return vec2(279086.0, 31812.0); // 2
            }
            return vec2(266527.0, 14896.0); // 3
        }
        if (g < 54.0) {
            return vec2(305544.0, 8479.0); // 4
        }
        return vec2(539711.0, 14896.0); // 5
    }
    if (g < 57.0) {
        if (g < 56.0) {
            return vec2(492620.0, 14897.0); // 6
        }
        return vec2(139807.0, 2114.0); // 7
    }
    if (g < 58.0) {
        return vec2(476718.0, 14897.0); // 8
    }
    return vec2(1001006.0, 6416.0); // 9
}

// Bit `index` (0..19) of a glyph row group.
float bitAt(float bits, float index) {
    return mod(floor(bits / exp2(index)), 2.0);
}

// 1 if the glyph covers dot (col, row) of its 5x7 box.
float glyphDot(vec2 bits, vec2 d) {
    if (d.x < 0.0 || d.y < 0.0 || d.x >= 5.0 || d.y >= 7.0) return 0.0;
    vec2 c = floor(d);
    if (c.y < 4.0) return bitAt(bits.x, c.y * 5.0 + c.x);
    return bitAt(bits.y, (c.y - 4.0) * 5.0 + c.x);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 w = f * f * (3.0 - 2.0 * f);
    float a = wl_hash12(i);
    float b = wl_hash12(i + vec2(1.0, 0.0));
    float c = wl_hash12(i + vec2(0.0, 1.0));
    float d = wl_hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}

float smootherX(float x) {
    x = clamp(x, 0.0, 1.0);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

vec3 glitchColor(float i) {
#if P_LIGHT_MODE
    vec3 c = i < 0.5 ? P_GLITCH_COLOR_1 : (i < 1.5 ? P_GLITCH_COLOR_2 : P_GLITCH_COLOR_3);
    return ink(c);
#else
    return i < 0.5 ? P_GLITCH_COLOR_1 : (i < 1.5 ? P_GLITCH_COLOR_2 : P_GLITCH_COLOR_3);
#endif
}

const float SLOT = 2.0;
const float GLYPHS = 59.0;

// Glitch event started by the cell `origin` during time slot `slot`, seen
// `k` cells to its right (row streaks light up to 8 cells, one after the
// other). Returns (heat, colour index).
vec2 glitchEvent(vec2 origin, float slot, float k, float t, float chance) {
    vec2 key = origin + vec2(slot * 37.13, slot * 11.71);
    vec2 r01 = wl_hash22(key + 0.37);
    if (r01.x >= chance) return vec2(0.0);
    vec2 r23 = wl_hash22(key + 19.19);
    vec2 r45 = wl_hash22(key + 47.71);
    vec2 r67 = wl_hash22(key + 83.29);
    float len = r67.x < 0.24 ? 2.0 + floor(r67.y * 7.0) : 1.0;
    if (k >= len) return vec2(0.0);
    float start = (slot + r01.y) * SLOT + k * 0.028;
    float tau = t - start;
    if (tau < 0.0) return vec2(0.0);
    float strength = 0.45 + 0.55 * r23.x;
    float hold = 0.04 + 0.26 * r23.y;
    float fade = 0.45 + 0.6 * r45.x;
    float h;
#if P_SMOOTH
    if (tau < hold) h = strength * (1.0 - exp(-tau / 0.05));
    else h = strength * (1.0 - exp(-hold / 0.05)) * exp(-4.0 * (tau - hold) / fade);
#else
    h = tau < hold ? strength : 0.0;
#endif
    if (h < 0.012) h = 0.0;
    return vec2(h, floor(r45.y * float(P_COLOR_COUNT)));
}

void main() {
    vec2 res = wl_res();
    vec2 frag = wl_frag();
    float t = wl_time();

    float size = P_FONT_SIZE;
    vec2 cellSize = vec2(max(2.0, ceil(size * 0.6 + size * P_SPACING)), max(2.0, ceil(size * (0.95 + P_SPACING))));
    vec2 grid = ceil(res / cellSize) + 1.0;
    vec2 origin = floor((res - grid * cellSize) / 2.0);
    // Component frame: top-left origin, y down.
    vec2 pos = vec2(frag.x, res.y - frag.y) - origin;
    vec2 cell = floor(pos / cellSize);
    vec2 inCell = pos - cell * cellSize;
    vec2 center = cell * cellSize + cellSize * 0.5 + origin;

    // --- Cell state ---------------------------------------------------
    float luck = wl_hash12(cell + vec2(3.1, 7.7));
    float level = 0.55 + 0.45 * wl_hash12(cell + vec2(11.3, 5.9));
    float restLevel = luck < P_DENSITY ? level : 0.0;
    float seed = wl_hash12(cell + 17.0);

    float heat = 0.0;
    float colorIndex = 0.0;
    float chance = pow(P_GLITCH_AMOUNT, 1.6) * 0.1 * SLOT;
    if (chance > 0.0) {
        float slot = floor(t / SLOT);
        for (int k = 0; k < 8; k++) {
            float fk = float(k);
            vec2 from = cell - vec2(fk, 0.0);
            for (int s = 0; s < 2; s++) {
                vec2 ev = glitchEvent(from, slot - float(s), fk, t, chance);
                if (ev.x > heat) {
                    heat = ev.x;
                    colorIndex = ev.y;
                }
            }
        }
    }

    // Letters reshuffle slowly at rest and quickly while glitching.
    float epoch = heat > 0.0
        ? floor(t / (0.045 + 0.45 * (1.0 - heat) * (1.0 - heat))) + 1000.0
        : floor(t * 0.2 + seed * 5.0);
    float glyph = floor(wl_hash12(cell * 1.31 + vec2(epoch * 0.731, epoch * 1.917)) * GLYPHS);
    vec2 bits = glyphBits(min(glyph, GLYPHS - 1.0));

    // 5x7 dot glyph, cap height 0.7em, centred in the cell; 2x2 supersampled.
    float dotSize = size * 0.1;
    vec2 glyphOrigin = cellSize * 0.5 - vec2(2.5, 3.5) * dotSize;
    float coverage = 0.0;
    coverage += glyphDot(bits, (inCell + vec2(0.25, 0.25) - glyphOrigin) / dotSize);
    coverage += glyphDot(bits, (inCell + vec2(0.75, 0.25) - glyphOrigin) / dotSize);
    coverage += glyphDot(bits, (inCell + vec2(0.25, 0.75) - glyphOrigin) / dotSize);
    coverage += glyphDot(bits, (inCell + vec2(0.75, 0.75) - glyphOrigin) / dotSize);
    coverage *= 0.25;

    vec4 color = vec4(0.0);
    if (coverage > 0.0) {
        float mask = 1.0;
        if (P_EDGE_FADE > 0.0) {
            vec2 edge = min(center, res - center) / (P_EDGE_FADE * min(res.x, res.y));
            mask = smootherX(edge.x) * smootherX(edge.y);
        }
        if (P_CENTER_FADE > 0.0) {
            vec2 room = max(abs(center - res * 0.5) - res * vec2(0.3, 0.24), 0.0);
            float soft = min(res.x, res.y) * 0.24;
            mask *= 1.0 - P_CENTER_FADE * (1.0 - smootherX(length(room) / soft));
        }
        float rest = restLevel * P_BASE_OPACITY * (0.8 + 0.2 * sin(t * (0.5 + 1.2 * seed) + seed * 40.0));
        if (P_FLOW > 0.0) {
            vec2 p = center / (max(res.x, res.y) * 0.32);
            float n = 0.65 * valueNoise(p + vec2(t * 0.05, t * 0.03)) + 0.35 * valueNoise(p * 2.03 - vec2(t * 0.04, -t * 0.06));
            rest *= mix(1.0, 0.1 + 1.7 * smoothstep(0.25, 0.8, n), P_FLOW);
        }
        rest = min(rest, 1.0) * (1.0 - heat);
        float weight = coverage * mask;
#if P_LIGHT_MODE
        vec3 baseColor = ink(P_BASE_COLOR);
#else
        vec3 baseColor = P_BASE_COLOR;
#endif
        color = vec4((baseColor * rest + glitchColor(colorIndex) * heat) * weight, (rest + heat) * weight);
    }
#if P_LIGHT_MODE
    vec3 bg = dot(P_BACKGROUND, vec3(0.2126, 0.7152, 0.0722)) > 0.5 ? P_BACKGROUND : vec3(0.98);
#else
    vec3 bg = P_BACKGROUND;
#endif
    gl_FragColor = vec4(wl_over(bg, color), 1.0);
}
