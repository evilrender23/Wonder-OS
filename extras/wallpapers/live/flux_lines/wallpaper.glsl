// Flux Lines — ported from React Bits <FluxLines /> (see ../LICENSE-REACTBITS.md).
// Already a single pass: the field, rim and core are the original's. The
// cursor magnet, click swirl and intro fade are dropped; the palette takes
// three colours instead of a list of up to eight.

float hash(vec3 p3) {
  p3 = fract(p3 * 0.1031);
  p3 += dot(p3, p3.zyx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

float smin(float a, float b, float k) {
  float r = exp2(-a / k) + exp2(-b / k);
  return -k * log2(r);
}

float sinlerp(float a, float b, float w) {
  return mix(a, b, (sin(w * 3.14159265 - 3.14159265 / 2.0) + 1.0) / 2.0);
}

float vn(vec2 p, float s, float seed) {
  vec2 cellp = floor(p / s);
  vec2 relp = p - cellp * s;
  float g1 = hash(vec3(cellp, seed));
  float g2 = hash(vec3(cellp.x + 1.0, cellp.y, seed));
  float g3 = hash(vec3(cellp.x + 1.0, cellp.y + 1.0, seed));
  float g4 = hash(vec3(cellp.x, cellp.y + 1.0, seed));
  float bx = sinlerp(g1, g2, relp.x / s);
  float tx = sinlerp(g4, g3, relp.x / s);
  return sinlerp(bx, tx, relp.y / s);
}

float dbn(vec2 p, float s, float seed) {
  float o = s / 2.0;
  float n0 = vn(p, s, seed);
  float n1 = vn(p + vec2(o, o), s, seed + 0.1);
  float n2 = vn(p + vec2(-o, o), s, seed + 0.2);
  float n3 = vn(p + vec2(o, -o), s, seed + 0.3);
  float n4 = vn(p + vec2(-o, -o), s, seed + 0.4);
  return (2.0 * n0 + 1.5 * n1 + 1.25 * n2 + 1.125 * n3 + n4) / 7.0;
}

vec3 palette(float h) {
  float x = clamp(h, 0.0, 1.0) * 2.0;
  if (x < 1.0) return mix(P_COLOR_1, P_COLOR_2, smoothstep(0.0, 1.0, x));
  return mix(P_COLOR_2, P_COLOR_3, smoothstep(0.0, 1.0, x - 1.0));
}

vec2 flow_dir() {
  if (P_FLOW_DIRECTION == 0) return vec2(0.0, 1.0);
  if (P_FLOW_DIRECTION == 2) return vec2(-1.0, 0.0);
  if (P_FLOW_DIRECTION == 3) return vec2(1.0, 0.0);
  return vec2(0.0, -1.0);
}

void main() {
  vec2 res = wl_res();
  float ref = 700.0 / max(P_SCALE, 0.05);
  vec2 p = wl_frag() / res.y * ref;

  // uTime runs in seconds; spd = 200 * speed (default 0.5).
  float spd = 100.0;
  float t = wl_time();
  vec2 dir = flow_dir();
  vec2 perp = vec2(-dir.y, dir.x);
  float distort1 = vn(p + perp * (t * spd), 60.0, 10.0) * 50.0 * P_TURBULENCE;
  float distort2 = vn(p - perp * (t * spd), 120.0, 15.0) * 100.0 * P_TURBULENCE;
  float peaks = dbn(p + distort1 + dir * (t * spd * 0.5), 40.0, 1.0);
  float peaks2 = dbn(p + distort2 - dir * (t * spd * 0.5), 40.0, 0.0);
  float field = smin(peaks, peaks2, max(P_FLUIDITY, 0.001));

  float slope = length(vec2(dFdx(field), dFdy(field)));
  float gap = abs(field - 0.4);
  float pixels = gap / max(slope, 1e-6);
  float half_band = max(P_RIM_WIDTH, 0.001) * 0.5;
  float across = max(gap / half_band, pixels / 28.0);
  float band = 5.0 * P_RIM_WIDTH * (1.0 - across);
  float shimmer = vn(p + dir * (t * spd * 0.5), 60.0, 12.0) * P_SHIMMER;
  float lit = clamp(band - shimmer, 0.0, 1.0);
  float glow = pow(lit, P_SHARPNESS) * P_GLOW;
  float line = P_LINE_WIDTH * 0.5;
  float core = line > 0.0 ? smoothstep(line + 0.75, line - 0.75, pixels) * smoothstep(0.08, 0.6, glow) : 0.0;
  float shine = clamp(glow, 0.0, 1.0);
  float energy = max(shine * shine, core * 0.85);

  float h = clamp(0.5 + (peaks - peaks2) * 0.8, 0.0, 1.0);
  vec3 tint = palette(h);

  vec3 bg = P_BACKGROUND;
  vec3 color;
  if (P_LIGHT_MODE == 1) {
    // Light mode wants a light page; a dark background turns into paper.
    if (dot(bg, vec3(0.2126, 0.7152, 0.0722)) < 0.5) bg = vec3(1.0);
    float chroma = max(tint.r, max(tint.g, tint.b)) - min(tint.r, min(tint.g, tint.b));
    vec3 ink = mix(vec3(0.07, 0.07, 0.08), tint * 0.55, clamp(chroma * 1.6, 0.0, 1.0));
    color = mix(bg, ink, clamp(energy, 0.0, 1.0));
  } else {
    vec3 light = tint * energy;
    color = wl_over(bg, vec4(light, max(light.r, max(light.g, light.b))));
  }
  gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
