# WonderlandWM — Fase 0: auditoría y medición base

Base: driftwm `e97a9d0` (v0.21.0, 2026-10-28). Fecha: 2026-11-09.
Datos crudos: `dev/bench/results/` (JSON + capturas; las trazas Tracy no se versionan).

## 1. Arquitectura

- **lib** (lógica pura, sin smithay): `canvas`, `layout/`, `stage/`, `config/`, `protocols/`, `text`.
- **bin** (estado del compositor): `state/`, `backend/` (winit, udev), `render/`, `handlers/`, `input/`, `grabs/`, `ipc/`, y el arnés `src/tests/`.
- Flujo: calloop → libinput/winit → `input/` + `grabs/` → API de `DriftWm` → `stage/`
  (única fuente de verdad de ventanas; `Space` solo registra outputs, un lint de clippy lo impone)
  → `render::compose_frame` → `OutputDamageTracker`/`DrmCompositor` → `post_render` (frame callbacks).
- **Cámara**: `OutputState { camera, zoom, camera_target, zoom_target, … }` (`src/state/mod.rs:501`),
  una por output, en un `Mutex` dentro del `user_data` del output. Pantalla = `(w − c)·z`.
  Cámara y zoom se animan con lerp exponencial independiente (`src/state/viewport_animation.rs:461`).

## 2. Lo que el plan pedía y ya existe

| Plan | Estado |
|---|---|
| Backend headless + arnés de clientes | Existe: `src/tests/` (~75 escenarios, clientes wayland reales, snapshots IPC) |
| Proptests de geometría | Parcial: snap, fill, stage, config. **Faltan** canvas↔pantalla, cluster, auto-placement, fit |
| Prueba de resistencia | Parcial: soak headless de 30 ciclos; sin render ni 8 h |
| Retener frame callbacks fuera de cámara | Existe: latido de 995 ms (`src/render/lifecycle.rs:11`) |
| Direct scanout | Existe: `ALLOW_PRIMARY_PLANE_SCANOUT_ANY` (`src/backend/udev.rs:1552`) |
| ext-workspace | Existe (exporta bookmarks) |
| Hot-reload con errores | Existe, con barra de error |

## 3. Deuda técnica

**Panics fuera de tests: 265.** ~110 son `seat.get_*().unwrap()` (infalibles por construcción,
`src/state/init.rs:229-270`); ~25 `lock().unwrap()`; los `assert!` de invariantes son solo debug.
Alcanzables por cliente o driver:

- `src/protocols/virtual_keyboard.rs:578` — `Seat::from_resource(&seat).unwrap()`
- `src/protocols/screencopy.rs:175,204`, `src/render/capture.rs:106,452` — `current_mode().unwrap()`
- `src/render/background.rs:748,832` — `expect("Default shader must compile")`
- `src/handlers/mod.rs:161,167` — `grab_start_data().unwrap()` en DnD
- `backend.take().unwrap()` (`src/backend/winit.rs:58,214`, `src/backend/udev.rs:692,1497`): panic si se reentra
- Sin panic hook ni recuperación: un panic en udev tira la sesión.

**Funciones de >300 líneas:** `src/render/mod.rs:731 compose_frame` (1183), `src/render/blur.rs:1031 process_blur_requests` (928),
`src/config/mod.rs:489 from_raw_collect` (782), `src/handlers/compositor.rs:207 commit` (744),
`src/input/actions.rs:174 execute_action` (575), `src/backend/udev.rs:409 init_udev` (546),
`src/input/pointer.rs:210 on_pointer_button` (543), `src/config/defaults.rs:7 default_bindings` (439),
`src/render/mod.rs:303 compose_capture_elements` (425), `src/main.rs:80 main` (369),
`src/state/init.rs:74 new_with_config` (368), `src/state/fullscreen.rs:60 enter_fullscreen` (302).

**Duplicación:** dos composiciones paralelas (`compose_frame` / `compose_capture_elements`, con tests de
«paridad» para mantenerlas iguales); dos cascadas de hit-test (`focus_under` en
`src/state/viewport_animation.rs:96` y `focus_cascade` en `src/input/mod.rs:565`); `DriftWm` con 154 campos
públicos (`src/state/mod.rs:672`); búsquedas lineales del stage (`src/stage/mod.rs:162,176,224,289`)
llamadas dentro de bucles por ventana → O(N²) por frame.

**Sin tests:** `src/backend/udev.rs` (1936 líneas), el camino de render (salvo píxeles de chrome),
conversiones canvas↔pantalla sin proptest.

**Otros:** bucle winit con timer fijo de 16 ms (`src/backend/winit.rs:313`) que no descuenta el frame
(baja a ~53 fps con carga); bakes síncronos de chunks del fondo (un frame de 1,49 s medido con `cache_shader`).

## 4. Medición base

Montaje: sesión anidada (winit) sobre KDE, ventana 1280×800; `dev/bench/client` abre N toplevels SHM de
640×400; `dev/bench/run_baseline.py` panea la cámara por IPC 15 s y captura con Tracy.
CPU = zona `winit::frame`; GPU = zona GPU `render` de smithay.

**Radeon 610M (iGPU, Mesa radeonsi) — medición de referencia**

| Ventanas / escenario | CPU p50 / p99 ms | GPU p50 / p99 ms | RSS MiB | VRAM MiB |
|---|---|---|---|---|
| 10, paneo | 0,21 / 0,67 | 0,93 / 1,98 | 133 | 92 |
| 50, vista general | 0,53 / 1,03 | 1,51 / 1,71 | 197 | 195 |
| 200, paneo | 0,53 / 1,22 | 1,17 / 2,02 | 197 | 238 |
| 200, zoom 0,4 (≈10 visibles, máx 30) | 0,71 / 1,50 | 0,78 / 2,10 | 228 | 261 |
| 200, zoom 0,4, clientes animados | 1,22 / 4,02 | 1,17 / 2,46 | 305 | 282 |
| 200, vista general | 1,44 / 2,59 | 1,86 / 2,00 | 345 | 574 |

La RTX 4090 compartía GPU con KDE (37 % de uso en reposo): sus p99 (13–18 ms) no son fiables.
La latencia de input no se midió (el timer de 16 ms de winit la domina); se propone instrumentarla en udev.

CI local (`dev/scripts/ci-local`): fmt, `check --locked`, clippy `-D warnings`, tests y soak en verde —
2383 tests, 0 fallos, 133 s.

## 5. Correcciones propuestas al plan

1. Fase 1: ampliar el arnés existente (proptests que faltan, accesores sin `unwrap`, soak con render,
   caos de 8 h anidado sobre `dev/bench`) en vez de construir otro.
2. «Cero panics, todo se recupera» no es literal para invariantes rotas: eliminar los panics alcanzables
   por cliente/driver y abortar con estado guardado en el resto (la restauración de sesión ya existe).
3. Fase 2: el p99 < 4 ms ya se cumple con clientes estáticos; centrarse en clientes animados, VRAM de la
   vista general y bakes síncronos. Un R-tree ahorraría < 0,5 ms con 200 ventanas.
4. Cambio de nombre solo en lo visible (binario, `.desktop`, `~/.config/wonderlandwm` con fallback,
   socket IPC): `driftwm` aparece 776 veces en 159 archivos y upstream sigue muy activo.
