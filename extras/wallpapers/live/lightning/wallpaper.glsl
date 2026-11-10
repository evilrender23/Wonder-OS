// Lightning — ported from React Bits <Lightning /> (see ../LICENSE-REACTBITS.md).
// Already a single pass: bolts, branches, flicker and glow are the
// original's. The cursor bend/crackle, click strikes and the intro descent
// are dropped. The motion loops every 10 s at speed 1, as in the component.

const float TAU = 6.28318530718;
const float PERIOD = 8.0;
const float WARP = 2.1;

float g_phase;
vec2 g_drift;
float g_width;
float g_core;
float g_pixel;

vec2 hash22(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.xx + p3.yz) * p3.zy) * 2.0 - 1.0;
}

float hash11(float p) {
  p = fract(p * 0.1031);
  p *= p + 33.33;
  p *= p + p;
  return fract(p);
}

float gnoise(vec2 p, float period) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  float a = dot(hash22(mod(i, period)), f);
  float b = dot(hash22(mod(i + vec2(1.0, 0.0), period)), f - vec2(1.0, 0.0));
  float c = dot(hash22(mod(i + vec2(0.0, 1.0), period)), f - vec2(0.0, 1.0));
  float d = dot(hash22(mod(i + vec2(1.0, 1.0), period)), f - vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float warp_field(vec2 q, float base, float reach, int octaves) {
  float value = 0.0;
  float amplitude = 0.5;
  float period = PERIOD;
  for (int i = 0; i < 8; i++) {
    if (i >= octaves) break;
    float weight = 1.0;
    if (i >= 3) {
      float estimate = abs(base + value * WARP);
      weight = 1.0 - smoothstep(reach * 0.6, reach, estimate);
      if (weight <= 0.0) break;
      reach *= 0.62;
    }
    value += amplitude * gnoise(q, period) * weight;
    q = q * 2.0 + vec2(17.0, 31.0);
    period *= 2.0;
    amplitude *= 0.5;
  }
  return value * WARP;
}

float flicker(float seed) {
  float steady = 0.82 + 0.18 * sin(TAU * (g_phase * 13.0 + seed)) * sin(TAU * (g_phase * 7.0 + seed * 1.7));
  float burst = 0.0;
  for (int j = 0; j < 3; j++) {
    float at = hash11(seed * 17.0 + float(j) * 5.3);
    float since = fract(g_phase - at) * 10.0;
    burst += exp(-since * 7.0) * (0.75 + 0.45 * sin(since * 70.0));
  }
  return mix(1.0, steady + burst * 0.9, P_FLICKER);
}

float glow(float d, float width) {
  float reach = d / (width * 3.5);
  return width / (d + width * 0.08) / (1.0 + reach * reach);
}

vec3 bolt(vec2 p, vec2 offs, float seed, float strength, float branches, int octaves) {
  vec2 q = p * P_SIZE + g_drift + offs;
  float warp = warp_field(q, p.x, 1.3 * max(1.0, P_GLOW), octaves);
  float x = p.x + warp;
  float d = abs(x);
  float energy = glow(d, g_width) * strength;
  float core = (1.0 - smoothstep(g_core - g_pixel, g_core + g_pixel, d)) * min(strength, 1.6);
  float line = glow(d, g_width * 0.55) * strength;

  for (int k = 0; k < 6; k++) {
    float order = float(k) + 0.5;
    if (order > branches) break;
    float h = seed * 31.0 + float(k) * 13.7;
    float start = mix(-0.75, 0.9, hash11(h));
    float span = mix(0.22, 0.7, hash11(h + 3.0));
    float along = (start - p.y) / span;
    if (along <= 0.0 || along >= 1.0) continue;
    float side = hash11(h + 1.0) > 0.5 ? 1.0 : -1.0;
    float slope = mix(0.35, 1.15, hash11(h + 2.0));
    float off = x - side * slope * (start - p.y);
    if (abs(off) - 0.12 * along > g_width * 13.0) continue;
    float window = fract(g_phase * 3.0 + hash11(h + 4.0));
    float gain = smoothstep(0.0, 0.03, window) * (1.0 - smoothstep(0.3, 0.42, window));
    if (gain <= 0.001) continue;
    gain *= flicker(seed + order);
    if (gain <= 0.001) continue;
    vec2 jq = q * 3.0 + vec2(order * 11.0, order * 5.0);
    float jag = (gnoise(jq, PERIOD * 3.0) * 0.12 + gnoise(jq * 3.0 + vec2(5.0, 9.0), PERIOD * 9.0) * 0.035) * along;
    float bd = abs(off - jag);
    float taper = pow(1.0 - along, 1.6) * smoothstep(0.0, 0.2, along);
    energy += glow(bd, g_width * 0.4) * taper * gain;
    line += glow(bd, g_width * 0.2) * taper * gain;
    core += (1.0 - smoothstep(g_core * 0.6 - g_pixel, g_core * 0.6 + g_pixel, bd)) * taper * min(gain, 1.0) * 0.8;
  }
  return vec3(energy, core, line);
}

void main() {
  vec2 res = wl_res();
  vec2 frag = wl_frag();
  vec2 uv = (2.0 * frag - res) / res.y;
  // phase advances speed/10 per second from 0.21 and wraps at 1.
  g_phase = fract(0.21 + wl_time() * 0.1);
  g_pixel = 2.0 / res.y;
  g_drift = vec2(PERIOD) * g_phase;
  g_width = 0.07 * max(P_GLOW, 0.01);
  g_core = 0.0032 * P_THICKNESS;
  float ang = radians(P_ANGLE);
  float ca = cos(ang);
  float sa = sin(ang);
  vec2 local = mat2(ca, -sa, sa, ca) * uv;
  int octaves = res.y > 1400.0 ? 8 : 7;

  float energy = 0.0;
  float core = 0.0;
  float line = 0.0;
  for (int b = 0; b < 4; b++) {
    if (b >= P_BOLTS) break;
    float seed = float(b) * 7.13 + 0.37;
    float lane = P_X_OFFSET + (float(b) - 0.5 * float(P_BOLTS - 1)) * P_SPREAD;
    vec2 p = local - vec2(lane, 0.0);
    vec3 hit = bolt(p, vec2(float(b) * 3.0, float(b) * 5.0), seed, flicker(seed), P_BRANCHES * 6.0, octaves);
    energy += hit.x;
    core += hit.y;
    line += hit.z;
  }

  float e = energy * P_INTENSITY * 0.6;
  float hot = clamp(core + smoothstep(6.0, 20.0, e) * 0.55, 0.0, 1.0);
  vec2 edge = min(frag, res - frag) / (res.y * max(P_FADE, 0.0001) * 0.5);
  float fade = P_FADE > 0.0 ? smoothstep(0.0, 1.0, edge.x) * smoothstep(0.0, 1.0, edge.y) : 1.0;

  vec3 bg = P_BACKGROUND;
  vec3 rgb;
  float alpha;
  if (P_LIGHT_MODE == 1) {
    // Light mode wants a light page; a dark background turns into paper.
    if (dot(bg, vec3(0.2126, 0.7152, 0.0722)) < 0.5) bg = vec3(0.965, 0.965, 0.973);
    float stroke = smoothstep(1.0, 3.6, line * P_INTENSITY);
    float halo = 1.0 - exp(-e * 0.35);
    vec3 ink = P_COLOR * 0.82;
    alpha = clamp(stroke + halo * 0.15, 0.0, 1.0);
    rgb = mix(ink, vec3(1.0), hot * 0.85) * alpha;
  } else {
    vec3 lit = 1.0 - exp(-P_COLOR * e * 0.9);
    rgb = mix(lit, vec3(1.0), hot);
    alpha = clamp(max(rgb.r, max(rgb.g, rgb.b)), 0.0, 1.0);
  }
  gl_FragColor = vec4(wl_over(bg, vec4(rgb, alpha) * fade), 1.0);
}
