# WonderlandWM — Fase 1: fundaciones

Rama `fase-1/fundaciones` (sobre `extras/barra-rise`). Sin features nuevas.

## Commits

| Commit | Qué |
|---|---|
| `Fase 1: no panics outside tests` | 242 sitios de pánico fuera de tests → recuperación con log; lint que lo impone |
| `fix(bar): send window ids bare…` | Arreglo de la barra (formato de `WindowSelector`) |
| `tests: properties for the canvas…` | Proptests de canvas, clusters y fit-window |
| `bench: chaos soak for nested sessions` | `chaos_soak.py` + `drift-vkbd` |

## 1. Cero pánicos en render/input

Inventario de partida (auditoría de la Fase 0): 265 sitios fuera de tests, 242 sin contar `assert!`.

| Clase | Sitios | Arreglo |
|---|---|---|
| `seat.get_pointer/keyboard/touch().unwrap()` | 103 | Handles guardados en `DriftWm` al iniciar (`pointer`, `keyboard`, `touch`): el `Option` desaparece por construcción |
| `Mutex::lock().unwrap()` / `Condvar::wait().unwrap()` | 36 | `driftwm::sync::{MutexExt, CondvarExt}`: un mutex envenenado se recupera con aviso |
| Backend sacado/devuelto (`backend.take().unwrap()`) | 7 | Si falta (reentrada), se salta el frame con error; en el init, `Err` |
| Output sin modo (`current_mode().unwrap()`) | 5 | Captura: se queda en cola para el siguiente frame; screencopy: `failed()`; estado: se omite |
| Shaders que el driver rechaza | 6 | Fondo por defecto → `type = "none"`; blur → sin frost; chunk/tile → error en la barra |
| Peticiones de protocolo desconocidas (`_ => unreachable!()`) | 13 | Se ignoran con `debug!` |
| Datos de cliente/recurso (`data_map.get().unwrap()`, colas de screencopy, seat muerto del teclado virtual, dnd sin grab, `create_resource`) | ~20 | `let-else` / `if let`; el teclado virtual con seat muerto recibe error de protocolo |
| IPC (`Subscribe` en `dispatch`, ids de stage) | 6 | `Err` al cliente; `focused_window_info` devuelve `Option` |
| Guardas locales (`if x.is_some() { x.unwrap() }`, insertar-y-leer) | ~30 | `let-else` / `entry()` |
| Arranque (signalfd, ping de fuentes, dmabuf feedback) | 5 | Degradan con error (SIGTERM sin cierre limpio, títulos al siguiente redibujado) o `?` |

**Candado**: `lib.rs` y `main.rs` niegan `clippy::{unwrap_used, expect_used, panic, unreachable, todo, unimplemented}` fuera de `cfg(test)`. Clippy con `-D warnings` en verde.

**Invariantes que quedan** (5, con `#[expect(..., reason = "...")]`):
- `output_state()`: todo `Output` nace con su `OutputState`. La reentrada es un deadlock, y el pánico deliberado de upstream lo hace visible en vez de congelar la sesión.
- `TouchGrab::start_data()` de los grabs de mover y redimensionar: la implementación solo se instala desde el constructor táctil.
- `client_compositor_state()`: todo cliente se inserta con `ClientState`; el trait exige una referencia.
- Keymap `us` de reserva al arrancar: si no compila, no hay datos xkb y no hay teclado posible.

**Fuera de alcance, sin cambios**: pánicos por indexación o desbordamiento aritmético, y `debug_assert!`. Por ejemplo, smithay entra en pánico en `Size::from` negativo solo en debug.

**Test**: `src/tests/ipc_chaos.rs` manda secuencias aleatorias de peticiones (NaN, ∞, 1e308, ids inexistentes, acciones basura) a un fixture vivo.
- 3000 casos limpios.
- Si se reintroduce el antiguo `unreachable!()`, el test falla y el caso mínimo es `[Subscribe]`.

## 2. Tests de propiedades

| Archivo | Propiedades | Casos (local) |
|---|---|---|
| `tests/canvas_props_test.rs` | 19: ida y vuelta pantalla↔canvas (±1e9, todo el rango de zoom), monotonía, anclaje del zoom, inversas exactas regla/contenido/interno, `zoom_to_fit`, cobertura del rect visible, `is_point_visible`, `visible_fraction`, `snap_zoom`, punto más cercano y `minimal_reveal_camera` | 20 000 |
| `tests/cluster_props_test.rs` | 5: adyacencia simétrica, clusters como partición, `cluster_of` igual a BFS de referencia, la cascada de redimensión termina y queda quieta con delta 0 | 20 000 |
| `src/tests/fit_props.rs` | fit configura el área usable, aterriza a zoom 1 con el marco en pantalla y unfit devuelve el tamaño exacto; incluye salidas de tamaño impar | 2 000 |

Ninguna propiedad encontró un bug en el código. Los dos contraejemplos que aparecieron eran fallos del propio test (un `Size` negativo y comprobar la vista antes de que termine la animación de geometría). Sus semillas están commiteadas.

## 3. «Backend headless»: no se añade

El plan pedía un backend headless y un arnés con clientes de prueba e IPC. Ya existe y es mejor de lo que se habría construido:
- `src/tests/fixture.rs` ejecuta un `DriftWm` real sin backend.
- Usa clientes wayland reales por socketpair, IPC por `ipc::dispatch` y snapshots.
- Comprueba los invariantes del stage en cada dispatch y las fugas de contadores al hacer `Drop`.
- Es determinista y no necesita GPU; tiene más de 1600 tests.

Un backend headless fuera de proceso duplicaría ese arnés y sería menos determinista. Lo que el fixture no cubre es la GPU real y el tiempo real, y eso lo cubre el soak anidado.

## 4. Soak de 8 h con caos

`dev/bench/chaos_soak.py --hours 8 --gpu amd` (Radeon 610M, sesión anidada a 1280×800, semilla 20261109).

Caos cada 20–200 ms:
- Clientes que se abren, se matan con SIGKILL o con SIGTERM.
- Por IPC: mover, redimensionar, enfocar, cerrar y suspender ventanas; saltos de cámara, zoom y acciones.
- Atajos de teclado reales por `zwp_virtual_keyboard_v1`.

Hasta 60 ventanas vivas. Cada 15 min llega a un mismo estado de reposo y mide memoria.

**Intento 1** (`dev/bench/results/soak-intento1-21min/`):
- Duró 21 min: 10 733 operaciones, hasta 68 ventanas y 18 clientes vivos, **0 pánicos**.
- Terminó con código 0 porque la ventana anfitriona recibió una petición de cierre. driftwm no lo registraba; desde `d701fb7` deja una línea en el log.
- En reposo: RSS 97,4 → 100,9 MiB, fds 30 → 31 y contadores de depuración a cero. Es una sola muestra útil, así que **sin veredicto de fugas**.

Las 8 h quedan por repetir, ahora desde la sesión WonderlandWM.

## 5. Criterio de salida

| Criterio | Estado |
|---|---|
| CI local (`dev/scripts/ci-local`: fmt, clippy `-D warnings`, tests con ignorados, soak headless) | Verde: 2410 tests, 0 fallos |
| Benchmarks ≥ Fase 0 | _pendiente: se miden en la iGPU cuando termine el soak (comparten GPU)_ |
| Soak 8 h sin crash ni fuga > 5 % | _pendiente: el intento 1 se cortó a los 21 min por un cierre de la ventana anfitriona (0 pánicos)_ |
