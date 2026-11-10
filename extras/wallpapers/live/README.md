# Live wallpapers

Animated backgrounds for WonderlandWM, picked from Spotlight (type
«wallpapers») and tuned in the Wallpapers panel. Each one is a driftwm
background shader: it renders behind the windows inside the compositor, with
no extra process, and can scroll with the infinite canvas (`parallax`).

The designs are ports of [React Bits](https://reactbits.dev) backgrounds by
David Haz, under MIT + Commons Clause (`LICENSE-REACTBITS.md`): they ship as
part of WonderlandWM and must not be redistributed on their own or as a
wallpaper pack. `lib/` and the tooling are WonderlandWM's (GPL-3.0-or-later).

## Layout

```
live/
  lib/prelude.glsl        shared prelude (uniforms + helpers)
  lib/image.glsl          shader for the user's imported pictures (ids img-…)
  <id>/wallpaper.glsl     shader body
  <id>/manifest.json      name + settings schema
  <id>/preview.png        480x270, built by dev/scripts/wallpaper-check
```

`extras/scripts/wonderland-wallpaper` turns a body plus settings into the
shader driftwm loads (`render`), and `apply` writes it to
`~/.config/driftwm/wallpapers/current.glsl` and points `[background]` at it.

## Writing (porting) a wallpaper

### The generated shader

`render` emits, in order:

1. `#extension …` lines from `manifest.extensions` (e.g.
   `"GL_OES_standard_derivatives"` to get `dFdx`/`dFdy`/`fwidth`).
2. One `#define P_<KEY> <value>` per setting (key upper-cased), plus the
   globals `P_SPEED`, `P_PARALLAX`, `P_ZOOM_FOLLOW`, `P_FPS` and
   `WL_ANIMATED`.
3. `lib/prelude.glsl`: precision, `v_coords`, `size`, `u_camera`, `u_zoom`,
   `u_time`, and the helpers below.
4. `wallpaper.glsl`, your body. It must define `void main()` and write an
   **opaque** `gl_FragColor` (alpha 1).

driftwm prepends `#version 100`: the body is **GLSL ES 1.00**. Do not declare
`#version`, `precision` or any `uniform` in it.

Setting types and what the define looks like:

| type    | manifest fields                         | define                     |
| ------- | --------------------------------------- | -------------------------- |
| `float` | `min`, `max`, `step`, `default`         | `0.5` (always a float)     |
| `int`   | `min`, `max`, `step`, `default`         | `6`                        |
| `bool`  | `default`                               | `1` / `0` (`#if P_X`, `P_X == 1`) |
| `color` | `default` (`#rrggbb`)                   | `vec3(r, g, b)`, sRGB 0..1 |
| `enum`  | `options: [{value, label}]`, `default`  | index of the option, `0`…  |

### Prelude helpers

| helper                 | use instead of                                             |
| ---------------------- | ---------------------------------------------------------- |
| `wl_frag()`            | `gl_FragCoord.xy` — pixels, **bottom-left origin** like WebGL, parallax applied |
| `wl_res()`             | the canvas size in pixels (`uResolution`)                  |
| `wl_time()`            | the component's clock in seconds, already × speed setting  |
| `wl_hash12/22`, `wl_tanh`, `wl_smoother`, `wl_to_linear`, `wl_to_srgb`, `wl_over(bg, premul)` | missing built-ins |

Do not read `gl_FragCoord`, `v_coords`, `size` or `u_time` directly.

### Porting rules

- **Settings**: the component's visual props — colours, counts, sizes,
  shapes (enums), intensities, `lightMode` as a bool «Modo claro» when the
  component has one, `variation`/seed. Defaults = the props table defaults.
  Leave out: mouse/click/hover props, `intro`, `paused`, `dpr`, `className`,
  `style`, `opacity`, and `speed` (it is the global «Velocidad»: fold the
  component's default speed and its internal time factor into the body, e.g.
  `float t = wl_time() * 0.5;`). Labels and descriptions in Spanish.
  Ranges should stay usable: the extremes must still render something.
- **Transparent components** (default `backgroundColor: transparent`, or
  premultiplied output) get a `background` colour setting (default `#0d1117`
  unless the component's own default is opaque) and finish with
  `wl_over(P_BACKGROUND, premultiplied)`.
- **Single pass only.** Multi-pass tricks become analytic approximations:
  bloom/glow → a few extra samples of the bright field on rings, or a
  distance falloff; blur/depth of field → widen/soften the analytic edge;
  feedback/persistence/fluid sims → a time-smeared or noise-advected
  equivalent; instanced geometry (bars, particles, shards, dots) → loop over
  the few neighbouring grid cells around the pixel and evaluate each
  instance analytically. Aim for the closest look you can get; note what
  changed in `manifest.notes` (Spanish, one or two sentences).
- **No interaction**: drop pointer, click and intro animations (the
  wallpaper sits behind windows).
- **GLSL ES 1.00 gotchas**: no `uint`, bit ops, `%`, `texelFetch`,
  `textureSize`, `texture()`, `round`, `trunc`, `tanh`, `sinh`, `inverse`,
  `transpose`, `isnan`; no array constructors (`float[4](…)`) or const arrays
  — use a function with an if-chain; loops need constant bounds (`for (int
  i = 0; i < 8; i++) { if (i >= n) break; … }`); index arrays only with loop
  indices; `in`/`out` → `gl_FragColor`; integer hashes → float hashes
  (`wl_hash12`). `float` literals need a dot (`1.0`). Matrices are
  column-major like WebGL.
- **Budget**: `wallpaper-check` reports `nvidia_ms_1440p` (RTX 4090,
  2560x1440) and `radeon_ms_1080p` (Radeon 610M iGPU, 1920x1080). Keep the
  first under ~1.5 ms; if the second exceeds ~20 ms set `"heavy": true` in
  the manifest (the panel warns). Prefer fewer samples over fidelity when
  you have to choose.

### Checking

```
dev/scripts/wallpaper-check <id>
```

It renders the default shader, validates it with `glslangValidator`
(strict ES 1.00), compiles and renders it on NVIDIA and on radeonsi, times
it, writes `preview.png`, then renders every setting at its extremes and
lists `flat_variants` (images with no detail: all black or one colour).
Exit 0 and `"problems": []` is required; review flat variants — a few are
legitimate (e.g. density 0), most are bugs. Look at `preview.png` and
compare it with the component's look.

`dev/tools/wallshot/wallshot SHADER OUT.png --size WxH --time T` renders any
generated shader (`wonderland-wallpaper render <id> '{"key": v}' -o F`) at
any time if you need more frames.
