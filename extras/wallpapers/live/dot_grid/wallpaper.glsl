// Dot Grid — ported from React Bits <DotGrid /> (see ../LICENSE-REACTBITS.md).
// The original is a CPU spring/jelly simulation driven by the pointer and
// clicks. Behind windows there is no pointer, so a ghost cursor wanders over
// the grid and shockwaves go off on their own; their displacement, velocity
// and spring ringing are evaluated analytically per dot (no neighbour
// coupling, so `tension` is dropped). Each pixel looks at the 3x3 dots
// around its estimated rest position and draws them with the original
// capsule/rounded-square shape, swell, stretch and halo.

const float TRAVEL = 650.0;

float g_cell;
float g_t;
vec2 g_res;
float g_stiff;
float g_zeta;
float g_omega;

float smootherstep01(float t) {
  t = clamp(t, 0.0, 1.0);
  return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Ghost cursor: a slow Lissajous path over the screen.
vec2 ghost_pos(float t) {
  return g_res * vec2(0.5 + 0.38 * sin(t * 0.31 + 0.7) + 0.06 * sin(t * 0.83),
                      0.5 + 0.34 * sin(t * 0.23 + 2.1) + 0.05 * cos(t * 0.71));
}

vec2 ghost_vel(float t) {
  return g_res * vec2(0.38 * 0.31 * cos(t * 0.31 + 0.7) + 0.06 * 0.83 * cos(t * 0.83),
                      0.34 * 0.23 * cos(t * 0.23 + 2.1) - 0.05 * 0.71 * sin(t * 0.71));
}

// Offset (xy) and velocity (zw) of the dot resting at `rest`.
vec4 motion(vec2 rest) {
  vec2 offs = vec2(0.0);
  vec2 vel = vec2(0.0);

#if P_GHOST
  {
    vec2 c = ghost_pos(g_t);
    vec2 cv = ghost_vel(g_t);
    float speed = length(cv);
    vec2 e = rest - c;
    float dist = length(e);
    float radius = max(1.0, P_PROXIMITY);
    if (dist < radius) {
      // Steady state of the spring under the pointer push.
      float drive = 6500.0 * wl_tanh(speed / 900.0) * P_STRENGTH;
      float w = smootherstep01(1.0 - dist / radius);
      vec2 n = dist > 0.001 ? e / dist : vec2(0.0);
      vec2 dir = speed > 0.001 ? cv / speed : vec2(0.0);
      offs += w * drive * (dir + n * 0.6) / g_stiff;
      vel += cv * w * 0.6;
    }
  }
#endif

  if (P_SHOCK_INTERVAL > 0.0) {
    float period = P_SHOCK_INTERVAL * 2.0;
    float width = g_cell * 1.4;
    for (int k = 0; k < 2; k++) {
      float clock = g_t + float(k) * P_SHOCK_INTERVAL;
      float cycle = floor(clock / period);
      float age = clock - cycle * period;
      vec2 src = (0.1 + 0.8 * wl_hash22(vec2(cycle * 1.37 + float(k) * 9.1, float(k) * 3.7 + 0.5))) * g_res;
      vec2 e = rest - src;
      float dist = length(e);
      if (dist < 0.001) continue;
      vec2 n = e / dist;
      float front = age * TRAVEL;
      float u = (dist - front) / width;
      float amp_now = smootherstep01(1.0 - front / max(1.0, P_SHOCK_RADIUS)) * P_SHOCK_STRENGTH * g_cell * 0.24;
      if (abs(u) < 3.0) {
        float profile = exp(-u * u);
        offs += n * amp_now * profile;
        vel += n * (amp_now * 2.0 * u * profile / width) * TRAVEL;
      }
      // The kick leaves the springs ringing behind the front.
      float tau = (front - dist) / TRAVEL;
      if (tau > 0.0) {
        float amp_then = smootherstep01(1.0 - dist / max(1.0, P_SHOCK_RADIUS)) * P_SHOCK_STRENGTH * g_cell * 0.24;
        float wd = g_omega * sqrt(max(1.0 - g_zeta * g_zeta, 0.05));
        float env = exp(-g_zeta * g_omega * tau) * smoothstep(0.0, 0.05, tau);
        float ring = amp_then * 0.35 * env;
        offs += n * ring * sin(wd * tau);
        vel += n * ring * wd * cos(wd * tau);
      }
    }
  }
  return vec4(offs, vel);
}

void main() {
  g_res = wl_res();
  vec2 frag = wl_frag();
  g_t = wl_time();
  float size = max(0.5, P_DOT_SIZE);
  float spacing = P_GAP;
  g_cell = max(2.0, size + spacing);
  float zeta = 1.0 - 0.85 * clamp(P_BOUNCE, 0.0, 1.0);
  float omega = 4.0 / (zeta * max(0.2, P_RETURN_DURATION));
  g_zeta = zeta;
  g_omega = omega;
  g_stiff = omega * omega;

  // Same layout as the component (centred), extended to an endless grid.
  vec2 count = max(vec2(1.0), floor((g_res + spacing) / g_cell));
  vec2 start = (g_res - (count * g_cell - spacing)) * 0.5 + size * 0.5;

  // Where would this pixel's dot rest? Undo the displacement once.
  vec2 guess = frag - motion(frag).xy;
  vec2 base_id = floor((guess - start) / g_cell + 0.5);

  vec4 acc = vec4(0.0);
  float presence = 0.0;
#if P_GHOST
  presence = 1.0;
  vec2 ghost = ghost_pos(g_t);
#endif
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      vec2 rest = start + (base_id + vec2(float(i), float(j))) * g_cell;
      vec4 m = motion(rest);
      vec2 pos = rest + m.xy;
      vec2 v = m.zw;
      float energy = min(1.0, length(v) / 380.0 * 0.85 + length(m.xy) / (g_cell * 2.2) * 0.35);
#if P_GHOST
      float gd = length(pos - ghost);
      float radius_p = max(1.0, P_PROXIMITY);
      if (gd < radius_p) energy = max(energy, smootherstep01(1.0 - gd / radius_p) * 0.4 * presence);
#endif
      float radius = 0.5 * size * (1.0 + P_SWELL * energy);
      float speed = length(v);
      float angle = speed > 0.001 ? atan(v.y, v.x) : 0.0;
      float axis = floor(angle / 1.5707963 + 0.5) * 1.5707963;
      angle = axis + (angle - axis) * smoothstep(8.0, 70.0, speed);
      vec2 dir = vec2(cos(angle), sin(angle));
      float half_len = min(size * 2.0, speed * P_STRETCH * 0.02);
      vec2 rel = frag - pos;
      vec2 local = vec2(dot(rel, dir), dot(rel, vec2(-dir.y, dir.x)));
      float d;
#if P_SHAPE == 1
      float corner = radius * 0.3;
      vec2 q = abs(local) - vec2(half_len + radius, radius) + corner;
      d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - corner;
#else
      d = length(vec2(max(abs(local.x) - half_len, 0.0), local.y)) - radius;
#endif
      float body = clamp(0.5 - d, 0.0, 1.0);
      float halo = P_GLOW * energy * 0.5 * exp(-max(d, 0.0) / max(radius * 1.3, 0.6));
      float alpha = body + (1.0 - body) * halo;
      vec3 color = mix(P_BASE_COLOR, P_ACTIVE_COLOR, clamp(energy * 1.2, 0.0, 1.0));
      if (P_FADE > 0.0) {
        vec2 edge = min(pos, g_res - pos) / (P_FADE * 0.4 * min(g_res.x, g_res.y));
        alpha *= smootherstep01(edge.x) * smootherstep01(edge.y);
      }
      alpha = clamp(alpha, 0.0, 1.0);
      acc = vec4(color * alpha, alpha) + acc * (1.0 - alpha);
    }
  }
  gl_FragColor = vec4(wl_over(P_BACKGROUND, acc), 1.0);
}
