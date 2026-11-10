// Pixel Snow — ported from React Bits <PixelSnow /> (see ../LICENSE-REACTBITS.md).
// The CPU particle simulation becomes five analytic layers (one per flake
// size): each layer is a grid of cells scrolling down at that size's speed,
// columns get their own drift, and each cell may hold one flake that sways
// and twinkles. Flakes use the same pixel sprites on the same snow-pixel
// grid; the blurred glow pyramid becomes a two-gaussian halo per flake and
// the snow pile is the dune profile it settles into. No cursor or intro.

float sprite_row(float slot, float ay) {
#if P_VARIANT == 0
  if (slot == 0.0) { if (ay == 0.0) return 3.0; }
  if (slot == 1.0) { if (ay == 0.0) return 2.0; }
  if (slot == 2.0) { if (ay == 0.0) return 11.0; if (ay == 1.0) return 6.0; }
  if (slot == 3.0) { if (ay == 0.0) return 11.0; if (ay == 1.0) return 6.0; }
  if (slot == 4.0) { if (ay == 0.0) return 47.0; if (ay == 1.0) return 43.0; if (ay == 2.0) return 26.0; }
  if (slot == 5.0) { if (ay == 0.0) return 11.0; if (ay == 1.0) return 6.0; }
  if (slot == 6.0) { if (ay == 0.0) return 11.0; if (ay == 1.0) return 6.0; }
  if (slot == 7.0) { if (ay == 0.0) return 47.0; if (ay == 1.0) return 43.0; if (ay == 2.0) return 26.0; }
#elif P_VARIANT == 1
  if (slot == 0.0) { if (ay == 0.0) return 3.0; }
  if (slot == 1.0) { if (ay == 0.0) return 7.0; if (ay == 1.0) return 1.0; }
  if (slot == 2.0) { if (ay == 0.0) return 7.0; if (ay == 1.0) return 1.0; }
  if (slot == 3.0) { if (ay == 0.0) return 31.0; if (ay == 1.0) return 27.0; if (ay == 2.0) return 5.0; }
  if (slot == 4.0) { if (ay == 0.0) return 111.0; if (ay == 1.0) return 107.0; if (ay == 2.0) return 26.0; if (ay == 3.0) return 5.0; }
  if (slot == 5.0) { if (ay == 0.0) return 7.0; if (ay == 1.0) return 1.0; }
  if (slot == 6.0) { if (ay == 0.0) return 31.0; if (ay == 1.0) return 27.0; if (ay == 2.0) return 5.0; }
  if (slot == 7.0) { if (ay == 0.0) return 111.0; if (ay == 1.0) return 107.0; if (ay == 2.0) return 26.0; if (ay == 3.0) return 5.0; }
#elif P_VARIANT == 2
  if (slot == 0.0) { if (ay == 0.0) return 3.0; }
  if (slot == 1.0) { if (ay == 0.0) return 7.0; if (ay == 1.0) return 1.0; }
  if (slot == 2.0) { if (ay == 0.0) return 11.0; if (ay == 1.0) return 18.0; if (ay == 2.0) return 1.0; }
  if (slot == 3.0) { if (ay == 0.0) return 107.0; if (ay == 1.0) return 10.0; if (ay == 2.0) return 18.0; if (ay == 3.0) return 1.0; }
  if (slot == 4.0) { if (ay == 0.0) return 427.0; if (ay == 1.0) return 10.0; if (ay == 2.0) return 34.0; if (ay == 3.0) return 66.0; if (ay == 4.0) return 1.0; }
  if (slot == 5.0) { if (ay == 0.0) return 3.0; if (ay == 1.0) return 8.0; if (ay == 2.0) return 16.0; }
  if (slot == 6.0) { if (ay == 0.0) return 107.0; if (ay == 1.0) return 10.0; if (ay == 2.0) return 34.0; if (ay == 3.0) return 65.0; }
  if (slot == 7.0) { if (ay == 0.0) return 427.0; if (ay == 1.0) return 10.0; if (ay == 2.0) return 34.0; if (ay == 3.0) return 130.0; if (ay == 4.0) return 257.0; }
#endif
  return 0.0;
}

float sprite_size(float l) {
#if P_VARIANT == 0
  if (l < 0.5) return 1.0;
  if (l < 1.5) return 2.0;
  if (l < 2.5) return 3.0;
  if (l < 3.5) return 4.0;
  return 5.0;
#elif P_VARIANT == 1
  if (l < 0.5) return 1.0;
  if (l < 1.5) return 3.0;
  if (l < 2.5) return 4.0;
  if (l < 3.5) return 5.0;
  return 7.0;
#elif P_VARIANT == 2
  if (l < 0.5) return 1.0;
  if (l < 1.5) return 3.0;
  if (l < 2.5) return 5.0;
  if (l < 3.5) return 7.0;
  return 9.0;
#endif
}

float sprite_lit(float l) {
#if P_VARIANT == 0
  if (l < 0.5) return 1.0;
  if (l < 1.5) return 4.0;
  if (l < 2.5) return 9.0;
  if (l < 3.5) return 16.0;
  return 25.0;
#elif P_VARIANT == 1
  if (l < 0.5) return 1.0;
  if (l < 1.5) return 5.0;
  if (l < 2.5) return 12.0;
  if (l < 3.5) return 21.0;
  return 37.0;
#elif P_VARIANT == 2
  if (l < 0.5) return 1.0;
  if (l < 1.5) return 5.0;
  if (l < 2.5) return 11.0;
  if (l < 3.5) return 21.0;
  return 29.0;
#endif
}

float level_weight(float l) {
  if (l < 0.5) return 0.46;
  if (l < 1.5) return 0.27;
  if (l < 2.5) return 0.15;
  if (l < 3.5) return 0.08;
  return 0.04;
}
float level_max_weight(float l) {
  if (l < 0.5) return 0.7384;
  if (l < 1.5) return 0.27;
  if (l < 2.5) return 0.2097;
  if (l < 3.5) return 0.2387;
  return 0.2626;
}
float level_fall(float l) {
  if (l < 0.5) return 16.0;
  if (l < 1.5) return 24.0;
  if (l < 2.5) return 34.0;
  if (l < 3.5) return 48.0;
  return 66.0;
}
float level_light(float l) {
  if (l < 0.5) return 0.42;
  if (l < 1.5) return 0.58;
  if (l < 2.5) return 0.76;
  if (l < 3.5) return 0.92;
  return 1.0;
}
float bayer4(vec2 c) {
  vec2 m = mod(c, 4.0);
  float i = m.x + m.y * 4.0;
  if (i < 0.5) return 0.0;
  if (i < 1.5) return 8.0;
  if (i < 2.5) return 2.0;
  if (i < 3.5) return 10.0;
  if (i < 4.5) return 12.0;
  if (i < 5.5) return 4.0;
  if (i < 6.5) return 14.0;
  if (i < 7.5) return 6.0;
  if (i < 8.5) return 3.0;
  if (i < 9.5) return 11.0;
  if (i < 10.5) return 1.0;
  if (i < 11.5) return 9.0;
  if (i < 12.5) return 15.0;
  if (i < 13.5) return 7.0;
  if (i < 14.5) return 13.0;
  return 5.0;
}

float hash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}

float smootherstep_edge(float edge, float x) {
  float t = clamp(x / max(edge, 0.0001), 0.0, 1.0);
  return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Mark of the sprite pixel (0 empty, 1 'x', 2 'o', 3 '@'); sprites are
// symmetric, so rows store one quadrant in base 4 from the centre out.
float sprite_mark(float slot, float n, vec2 local) {
  float c = (n - 1.0) * 0.5;
  vec2 a = abs(local - c) - (mod(n, 2.0) < 0.5 ? 0.5 : 0.0);
  float row = sprite_row(slot, a.y);
  return floor(mod(floor(row / exp2(2.0 * a.x) + 0.001), 4.0));
}

float g_t;
float g_pix;
float g_body;
float g_core;
float g_halo;
float g_wsum;

void flake_layer(float level, vec2 p, vec2 gcell) {
  float pix = g_pix;
  float n = sprite_size(level);
  // Share of flakes of this size at the chosen size mix (pickLevel weights).
  float share = level_weight(level) * pow(2.2, (P_FLAKE_SIZE - 1.0) * (level - 1.5)) / g_wsum;

  float parallax = 1.0 - P_DEPTH * (1.0 - (level + 1.0) / 5.0) * 0.75;
  float s = sqrt(1100.0 / level_max_weight(level));
  // Halo: two gaussians standing in for the half- and eighth-res blur.
  float var = n * n / 12.0;
  float sig1 = sqrt(1.44 + var) * pix;
  float sig2 = sqrt(16.0 + var) * pix;
  float halo_reach = P_GLOW > 0.0 ? 2.5 * sig2 : 0.0;
  float rbase = n * 0.5 * pix + halo_reach;
  float cw = max(1.5 * s, 1.25 * rbase);
  float ch = max(s * s / cw, 1.1 * rbase);
  float occupancy = min(1.0, P_DENSITY * share / level_max_weight(level) * (cw * ch) / (s * s));
  float sway_max = max(0.0, cw - rbase - pix);
  float wind_x = P_WIND * 46.0 * parallax * g_t;

  vec2 q = p;
  q.x -= wind_x;
  float colf = q.x / cw;
  float col0 = floor(colf);
  float coln = fract(colf) < 0.5 ? col0 - 1.0 : col0 + 1.0;
  for (int ci = 0; ci < 2; ci++) {
    float col = ci == 0 ? col0 : coln;
    float drift = 0.7 + 0.6 * hash(vec2(col, level * 7.31 + 1.7));
    float vy = level_fall(level) * parallax * drift;
    float qy = q.y + vy * g_t;
    float rowf = qy / ch;
    float row0 = floor(rowf);
    float rown = fract(rowf) < 0.5 ? row0 - 1.0 : row0 + 1.0;
    for (int ri = 0; ri < 2; ri++) {
      float row = ri == 0 ? row0 : rown;
      vec2 id = vec2(col, row) + level * vec2(37.17, 11.83);
      vec2 h1 = wl_hash22(id);
      if (h1.x >= occupancy) continue;
      vec2 h2 = wl_hash22(id + 19.19);
      vec2 h3 = wl_hash22(id + 47.53);
      float rate = 0.6 + h2.x * 0.9;
      float phase = h2.y * 6.2831853;
      float sway = min(P_SWAY * (6.0 + level * 7.0) * drift / rate, sway_max);
      vec2 jit = vec2(max(0.0, cw - rbase - sway), max(0.0, ch - rbase)) * (h3 * 2.0 - 1.0) * 0.5;
      vec2 center = vec2((col + 0.5) * cw, (row + 0.5) * ch) + jit;
      center.x += sway * sin(g_t * rate + phase) + wind_x;
      center.y -= vy * g_t;

      float spark = pow(max(0.0, sin(g_t * 0.9 + h1.y * 100.0)), 60.0) * P_TWINKLE;
      float depth_light = 1.0 - P_DEPTH * (1.0 - level_light(level));
      float light = min(1.0, depth_light * (0.85 + spark * 0.6));

      vec2 origin = floor(center / pix) - floor(n * 0.5);
      vec2 local = gcell - origin;
      if (local.x >= 0.0 && local.y >= 0.0 && local.x < n && local.y < n) {
        float slot = level >= 2.0 && h3.x > 0.5 ? level + 3.0 : level;
        float mark = sprite_mark(slot, n, local);
        if (mark > 0.5) {
          g_body = max(g_body, light);
          g_core = max(g_core, light * (mark > 2.5 ? 1.0 : (mark > 1.5 ? 0.5 : 0.0)));
        }
      }
      if (P_GLOW > 0.0) {
        vec2 dv = (gcell + 0.5 - origin - n * 0.5) * pix;
        float d2 = dot(dv, dv);
        float lit = sprite_lit(level) * pix * pix;
        float a1 = min(1.0, lit / (6.2832 * sig1 * sig1)) * exp(-d2 / (2.0 * sig1 * sig1));
        float a2 = min(1.0, lit / (6.2832 * sig2 * sig2)) * exp(-d2 / (2.0 * sig2 * sig2));
        float window = 1.0 - smoothstep(0.6 * halo_reach, halo_reach, sqrt(d2));
        g_halo += light * (0.5 * a1 + 0.5 * a2) * window;
      }
    }
  }
}

void main() {
  vec2 res = wl_res();
  vec2 frag = wl_frag();
  g_t = wl_time();
  g_pix = float(P_PIXEL_SIZE);
  vec2 gcell = floor(frag / g_pix);
  vec2 p = (gcell + 0.5) * g_pix;

  g_wsum = 0.0;
  for (int i = 0; i < 5; i++) g_wsum += level_weight(float(i)) * pow(2.2, (P_FLAKE_SIZE - 1.0) * (float(i) - 1.5));
  g_body = 0.0;
  g_core = 0.0;
  g_halo = 0.0;
  if (P_DENSITY > 0.0) {
    for (int l = 0; l < 5; l++) flake_layer(float(l), p, gcell);
  }

#if P_ACCUMULATE
  {
    // Settled pile: the dune profile the component's drifts grow towards.
    float cap = max(4.0, ceil(res.y / g_pix) * 0.08);
    float i = gcell.x;
    float dune = 0.55 + 0.45 * sin(i * 0.045 + sin(i * 0.013) * 2.1) * sin(i * 0.021 + 1.3);
    float height = floor(cap * dune + 0.5);
    float row = gcell.y;
    float pile_light = 1.0 - P_DEPTH * 0.1;
    if (row < height) {
      float below = height - row;
      float crest = below <= 1.0 ? 1.0 : 0.0;
      float threshold = (bayer4(gcell) + 0.5) / 16.0;
      float shade = clamp((below - 1.0) / 9.0, 0.0, 1.0) * 3.0;
      float step3 = floor(shade) + step(threshold, fract(shade));
      float body = 0.94 - step3 * 0.13;
      float glint = below <= 3.0 ? step(0.992, hash(gcell + floor(g_t * 1.1 + hash(gcell.yx) * 9.0))) : 0.0;
      g_body = max(g_body, body * pile_light);
      g_core = max(g_core, (crest * 0.6 + glint) * pile_light);
    }
    if (P_GLOW > 0.0) g_halo += 0.45 * pile_light * exp(-max(row - height, 0.0) / 2.5);
  }
#endif

  vec2 uv = frag / res;
  float edge = P_FADE * 0.5;
  float mask = smootherstep_edge(edge, uv.x) * smootherstep_edge(edge, 1.0 - uv.x)
      * smootherstep_edge(edge, uv.y) * smootherstep_edge(edge, 1.0 - uv.y);
  float body = clamp(g_body * P_BRIGHTNESS, 0.0, 1.0);
  float core = clamp(g_core * P_BRIGHTNESS, 0.0, 1.0);
  float halo = clamp(g_halo * P_GLOW * P_BRIGHTNESS, 0.0, 1.0);

  vec3 bg = P_BACKGROUND;
  vec4 premul;
  if (P_LIGHT_MODE == 1) {
    // Light mode wants a light page; a dark background turns into paper.
    if (dot(bg, vec3(0.2126, 0.7152, 0.0722)) < 0.5) bg = vec3(0.965, 0.965, 0.973);
    vec3 tint = mix(P_COLOR, vec3(1.0), clamp(core / max(body, 0.001), 0.0, 1.0) * 0.45);
    float alpha = body * 0.95;
    premul = vec4(tint * alpha, alpha);
    float soft = halo * 0.22 * (1.0 - alpha);
    premul += vec4(P_COLOR * soft, soft);
    premul *= mask;
  } else {
    vec3 color = P_COLOR * (body * 0.82 + core * 0.18) + P_COLOR * halo * 0.55;
    color = 1.0 - exp(-color * 1.6);
    color *= mask;
    premul = vec4(color, max(color.r, max(color.g, color.b)));
  }
  gl_FragColor = vec4(wl_over(bg, premul), 1.0);
}
