// Grid Scan — ported from React Bits <GridScan /> (see ../LICENSE-REACTBITS.md).
// The room, grid, shading, floor reflection, LiDAR points and haze are the
// original's single scene pass. The quarter-res blurred bloom becomes a second
// evaluation of the lighting with lines and scan widened ~12 px (energy kept),
// the view only does its idle drift, and clicks/pings, hover, webcam, gyro
// and the intro scan are dropped.

struct Light {
  float grid;
  float fade;
  float core;
  float trail;
  float spill;
};

float g_aspect;
float g_depth;
vec3 g_cell;
float g_thick;
vec3 g_eye;
float g_scan_z;
float g_scan_dir;
float g_scan_alpha;
float g_scan_width;
float g_trail;

float hash(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}

vec2 hash2(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.103, 0.0973));
  q += dot(q, q.yzx + 33.33);
  return fract((q.xx + q.yz) * q.zy);
}

float lines(float across, float along, float period, float dash, float fa, float fb) {
  float u = across / period;
  float du = fa / period;
  float d = abs(fract(u + 0.5) - 0.5) / max(du, 1e-5);
  float dense = smoothstep(0.1, 0.4, du);
  if (P_LINE_STYLE == 2) {
    float c = along / dash * 5.0;
    float dc = fb / dash * 5.0;
    float e = abs(fract(c + 0.5) - 0.5) / max(dc, 1e-5);
    float cov = clamp(g_thick + 1.0 - length(vec2(d, e)), 0.0, 1.0);
    float mean = min(1.0, 3.1416 * (g_thick + 0.5) * (g_thick + 0.5) * du * dc);
    return mix(cov, mean, max(dense, smoothstep(0.1, 0.4, dc)));
  }
  float cov = clamp(g_thick + 0.5 - d, 0.0, 1.0);
  float mean = min(1.0, 2.0 * g_thick * du);
  if (P_LINE_STYLE == 1) {
    float c = along / dash * 4.0;
    float dc = fb / dash * 4.0;
    float e = abs(fract(c + 0.5) - 0.5);
    float m = clamp((0.25 - e) / max(dc, 1e-5) + 0.5, 0.0, 1.0);
    cov *= mix(m, 0.5, smoothstep(0.1, 0.4, dc));
    mean *= 0.5;
  }
  return mix(cov, mean, dense);
}

float depth_fade(float z) {
  float n = clamp(z / g_depth, 0.0, 1.0);
  return mix(1.0, (exp(-3.0 * n) - 0.0497871) / 0.9502129, P_DEPTH_FADE);
}

vec3 project(vec3 eye, vec3 o, int face, float plane) {
  vec3 d = o - eye;
  float t = face == 0 ? (plane - o.x) / d.x : (face == 1 ? (plane - o.y) / d.y : (plane - o.z) / d.z);
  return o + d * t;
}

void trace(vec3 o, vec3 d, out int face, out float plane, out vec3 h) {
  float sx = d.x > 0.0 ? 0.5 * g_aspect : -0.5 * g_aspect;
  float sy = d.y > 0.0 ? 0.5 : -0.5;
  float tx = abs(d.x) > 1e-6 ? (sx - o.x) / d.x : 1e9;
  float ty = abs(d.y) > 1e-6 ? (sy - o.y) / d.y : 1e9;
  float t = (g_depth - o.z) / d.z;
  face = 2;
  plane = g_depth;
  if (tx < t) {
    face = 0;
    plane = sx;
    t = tx;
  }
  if (ty < t) {
    face = 1;
    plane = sy;
    t = ty;
  }
  h = o + d * t;
}

vec2 face_uv(vec3 h, int face) {
  if (face == 0) return vec2(h.z, h.y + 0.5);
  if (face == 1) return vec2(h.x + 0.5 * g_aspect, h.z);
  return vec2(h.x + 0.5 * g_aspect, h.y + 0.5);
}

vec2 face_cell(int face) {
  return face == 0 ? g_cell.zy : (face == 1 ? g_cell.xz : g_cell.xy);
}

Light illuminate(vec3 h, int face, vec2 uv, vec2 foot, float blur) {
  Light l;
  vec2 cell = face_cell(face);
  vec2 f = foot * blur;
  float minor = max(lines(uv.x, uv.y, cell.x, cell.y, f.x, f.y), lines(uv.y, uv.x, cell.y, cell.x, f.y, f.x));
  float major = max(lines(uv.x, uv.y, cell.x * 4.0, cell.y, f.x, f.y),
                    lines(uv.y, uv.x, cell.y * 4.0, cell.x, f.y, f.x));
  l.grid = max(minor * 0.75, major) / blur;
  float z = h.z;
  l.fade = depth_fade(z);
  float z_foot = face == 0 ? f.x : (face == 1 ? f.y : 0.0);
  float width = max(g_scan_width, z_foot * 1.1);
  float dz = z - g_scan_z;
  l.core = exp(-dz * dz / (width * width)) * (face == 2 ? 0.45 : 1.0) * g_scan_alpha;
  float behind = -dz * g_scan_dir;
  l.trail = exp(-max(behind, 0.0) / g_trail) * smoothstep(-width, width * 0.5, behind) * g_scan_alpha;
  l.spill = exp(-dz * dz / 0.05) * g_scan_alpha;
  return l;
}

void over(inout vec4 acc, vec3 color, float alpha) {
  alpha = clamp(alpha, 0.0, 1.0);
  acc = vec4(color * alpha, alpha) + acc * (1.0 - alpha);
}

void paint(inout vec4 color, inout float energy, Light l, float gain, float lift, vec3 line_c, vec3 scan_c, vec3 core_c) {
  float base = l.grid * l.fade * lift;
  float far = l.fade * gain;
  float lit = l.grid * (l.core * 1.2 + l.trail * 0.75) * far;
  float sheen = (l.core * 0.14 + l.spill * 0.05 + l.trail * 0.03) * far;
  float laser = l.core * far;
  over(color, line_c, base);
  over(color, scan_c, sheen);
  over(color, scan_c, lit);
  over(color, core_c, laser * 0.9);
  energy += laser * 0.9 + lit * 0.5;
}

void main() {
  vec2 res = wl_res();
  vec2 frag = wl_frag();
  vec2 vuv = frag / res;
  float time = wl_time();
  bool light_mode = P_LIGHT_MODE == 1;

  // Colours: light mode swaps the untouched dark defaults for its own.
  vec3 line_c = P_LINES_COLOR;
  vec3 scan_c = P_SCAN_COLOR;
  if (light_mode && distance(line_c, vec3(0.302, 0.278, 0.341)) < 0.01) line_c = vec3(0.831, 0.831, 0.847);
  if (light_mode && distance(scan_c, vec3(0.490, 0.827, 0.988)) < 0.01) scan_c = vec3(0.008, 0.518, 0.780);
  vec3 core_c = light_mode ? scan_c : scan_c + (1.0 - scan_c) * 0.65;
  vec3 bg = P_BACKGROUND;
  if (light_mode && dot(bg, vec3(0.2126, 0.7152, 0.0722)) < 0.5) bg = vec3(0.98, 0.98, 0.985);

  // Room size (roomSize() in the component).
  g_aspect = res.x / res.y;
  float cy = 1.0 / floor(float(P_CELLS) + 0.5);
  float cx = g_aspect / max(1.0, floor(g_aspect / cy + 0.5));
  g_depth = max(cy, floor(P_DEPTH / cy + 0.5) * cy);
  g_cell = vec3(cx, cy, cy);
  g_thick = P_LINE_THICKNESS * 0.5;
  float px = 1.0 / res.y;

  // Idle head drift (aimEye without a pointer).
  float nx = sin(time * 0.21) * 0.2 + sin(time * 0.083 + 1.3) * 0.12;
  float ny = sin(time * 0.17 + 2.1) * 0.16;
  g_eye = vec3(nx * P_VIEW_SWAY * 0.22 * g_aspect, ny * P_VIEW_SWAY * 0.22, -1.25);

  // Auto scan loop: wait scan_delay, sweep for scan_duration.
  float period = P_SCAN_DELAY + P_SCAN_DURATION;
  float clock = time + P_SCAN_DELAY + P_SCAN_DURATION * 0.4 - 4.0;
  float sweep = floor(clock / period);
  float k = (clock - sweep * period - P_SCAN_DELAY) / P_SCAN_DURATION;
  g_scan_dir = (P_SCAN_DIRECTION == 1 || (P_SCAN_DIRECTION == 2 && mod(sweep, 2.0) > 0.5)) ? -1.0 : 1.0;
  float travel = k * (g_depth + 0.3) - 0.15;
  g_scan_z = g_scan_dir > 0.0 ? travel : g_depth - travel;
  g_scan_alpha = k < 0.0 ? 0.0 : min(1.0, k / 0.12) * min(1.0, (1.0 - k) / 0.12);
  g_scan_width = 0.006 * P_SCAN_WIDTH;
  g_trail = 0.04 + P_TRAIL * 1.1;

  vec3 p = vec3((vuv.x - 0.5) * g_aspect, vuv.y - 0.5, 0.0);
  vec3 d = p - g_eye;
  int face;
  float plane;
  vec3 h;
  trace(p, d, face, plane, h);
  vec3 hx = project(g_eye, p + vec3(px, 0.0, 0.0), face, plane);
  vec3 hy = project(g_eye, p + vec3(0.0, px, 0.0), face, plane);
  vec2 uv = face_uv(h, face);
  vec2 ux = face_uv(hx, face) - uv;
  vec2 uy = face_uv(hy, face) - uv;
  vec2 foot = vec2(length(vec2(ux.x, uy.x)), length(vec2(ux.y, uy.y)));
  Light l = illuminate(h, face, uv, foot, 1.0);

  vec4 color = vec4(0.0);
  float energy = 0.0;

  if (P_SHADING > 0.0) {
    vec2 gaps = face == 0
      ? vec2(0.5 - abs(h.y), g_depth - h.z)
      : (face == 1 ? vec2(0.5 * g_aspect - abs(h.x), g_depth - h.z) : vec2(0.5 * g_aspect - abs(h.x), 0.5 - abs(h.y)));
    vec2 crease = 1.0 - exp(-max(gaps, 0.0) / 0.16);
    float occlusion = crease.x * crease.y;
    float lamp = 1.0 / (1.0 + h.z * h.z * 0.9);
    float tilt = face == 1 ? (plane < 0.0 ? 1.1 : 0.85) : (face == 2 ? 0.8 : 1.0);
    float glow_spill = (l.spill * 0.5 + l.trail * 0.12) * l.fade;
    if (light_mode) {
      float shadow = (1.0 - lamp * tilt) * 0.09 + (1.0 - occlusion) * 0.2;
      over(color, vec3(0.0), shadow * P_SHADING * l.fade);
    } else {
      over(color, vec3(1.0), lamp * tilt * occlusion * 0.11 * P_SHADING * l.fade);
    }
    over(color, scan_c, glow_spill * P_SHADING * occlusion * 0.35);
  }

  if (face == 1 && plane < 0.0 && P_REFLECTION > 0.0) {
    vec3 mirror = vec3(g_eye.x, -1.0 - g_eye.y, g_eye.z);
    vec3 r = h - mirror;
    int face2;
    float plane2;
    vec3 h2;
    trace(h, r, face2, plane2, h2);
    vec2 uv2 = face_uv(h2, face2);
    vec2 ux2 = face_uv(project(mirror, hx, face2, plane2), face2) - uv2;
    vec2 uy2 = face_uv(project(mirror, hy, face2, plane2), face2) - uv2;
    vec2 foot2 = vec2(length(vec2(ux2.x, uy2.x)), length(vec2(ux2.y, uy2.y)));
    float reach = length(h2 - h);
    Light m = illuminate(h2, face2, uv2, foot2, 1.0 + reach * 4.0);
    float grazing = 1.0 - normalize(r).y;
    float gain = (0.3 + 0.7 * grazing * grazing * grazing * grazing) * P_REFLECTION * exp(-reach * 1.1) * (light_mode ? 0.6 : 1.0);
    float reflected = 0.0;
    paint(color, reflected, m, gain, 0.0, line_c, scan_c, core_c);
    energy += reflected * 0.6;
  }

  paint(color, energy, l, 1.0, 1.0, line_c, scan_c, core_c);

  float spark = 0.0;
  float halo = 0.0;
  if (P_POINTS == 1) {
    vec2 cell = face_cell(face);
    vec2 spacing = cell / 2.5;
    vec2 q = uv / spacing;
    vec2 id = floor(q);
    vec2 seed = id + vec2(float(face) * 13.7 + plane * 5.3, float(face) * 3.1);
    vec2 delta = (q - id - (0.2 + 0.6 * hash2(seed))) * spacing;
    float det = ux.x * uy.y - ux.y * uy.x;
    vec2 s = vec2(delta.x * uy.y - delta.y * uy.x, ux.x * delta.y - ux.y * delta.x) / det;
    float radius = 1.8 * mix(1.0, 0.55, clamp(h.z / g_depth, 0.0, 1.0));
    float spot = clamp(radius + 0.5 - length(s), 0.0, 1.0);
    float gap = min(spacing.x / foot.x, spacing.y / foot.y);
    float rr = hash(seed + 7.7);
    float flick = 0.75 + 0.25 * sin(time * (3.0 + rr * 5.0) + rr * 40.0);
    float weight = smoothstep(5.0, 10.0, gap) * smoothstep(0.25, 0.6, rr) * (0.3 + 0.7 * rr * rr) * flick;
    float hot = l.core * 2.5;
    float warm = l.trail * l.trail * 1.3;
    spark = spot * weight * (hot + warm) * l.fade;
    over(color, mix(scan_c, core_c, clamp(hot / max(hot + warm, 1e-4), 0.0, 1.0)), spark);
    energy += spark * 0.9;
    // Bloom share of the point: a soft disc a few pixels wide.
    float ls = length(s) / (radius * 3.5);
    halo = weight * (hot + warm) * l.fade * exp(-ls * ls) * 0.3;
  }

  float haze_amt = light_mode ? 0.03 : 0.05;
  if (g_scan_alpha > 0.0 && g_scan_z > 0.0 && h.z > g_scan_z) {
    vec3 c = p + d * (g_scan_z / d.z);
    float grain = hash(floor(c.xy * 180.0) + floor(time * 24.0) * 7.0);
    float edge = min(0.5 * g_aspect - abs(c.x), 0.5 - abs(c.y));
    float rim = exp(-edge / 0.06);
    float veil = haze_amt * g_scan_alpha * (0.45 + 0.9 * grain) * (0.55 + rim * 1.6);
    over(color, scan_c, veil * depth_fade(g_scan_z));
  }

  // Bloom: the original blurs the energy buffer at quarter resolution.
  // Re-light the same hit with every line and the scan ~12 px wide (divided
  // by the width, so the energy spreads instead of growing).
  if (P_GLOW > 0.0) {
    Light b = illuminate(h, face, uv, foot, 12.0);
    float far = b.fade;
    float lit = b.grid * (b.core * 1.2 + b.trail * 0.75) * far;
    float laser = b.core * far;
    // A wide gaussian of the scan core: its peak drops with the widening.
    float z_foot = face == 0 ? foot.x : (face == 1 ? foot.y : 0.0);
    float thin = max(g_scan_width, z_foot * 1.1);
    float wide = max(g_scan_width, z_foot * 13.0);
    float b_energy = clamp(laser * 0.9 * clamp(thin / wide * 3.0, 0.0, 1.0) + lit * 0.5 + halo, 0.0, 1.0);
    float glow_gain = P_GLOW * (light_mode ? 0.8 : 1.6);
    vec4 bloom = vec4(scan_c * b_energy, b_energy) * glow_gain;
    bloom.a = min(bloom.a, 1.0);
    bloom.rgb = min(bloom.rgb, vec3(bloom.a));
    color += bloom * (1.0 - color.a);
  }

  float noise = (hash(frag) - 0.5) / 255.0;
  color.a = clamp(color.a + noise, 0.0, 1.0);
  color.rgb = clamp(color.rgb + noise, vec3(0.0), vec3(color.a));
  gl_FragColor = vec4(wl_over(bg, color), 1.0);
}
