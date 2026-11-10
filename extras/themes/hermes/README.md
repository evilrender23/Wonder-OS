# Tema Hermes para WonderlandWM

Estética de **Hermes Agent Desktop** aplicada al compositor, en variante clara y oscura.
Solo usa opciones de configuración y shaders de fondo: no cambia código del compositor.

## De dónde salen los valores

Se sacaron por ingeniería inversa estática de la app instalada (`/usr/lib/hermes-agent-desktop/app.asar`, v0.23.0):

| Fuente dentro del asar | Qué aporta |
|---|---|
| `dist/assets/storage-*.js`, tabla de temas, entrada `nous` (el tema por defecto, `Fe="nous"` en `context-*.js`) | Colores base claro y oscuro: fondo `#ffffff`/`#0d1117`, primario `#0053fd`/`#4a84fe`, bordes `#d0d7de`/`#30363d`, barra lateral `#f6f8fa`/`#010409` |
| `dist/assets/main-*.css`, bloques `:root` | Tokens derivados: `--ui-bg-chrome`, `--ui-bg-sidebar`, trazos tintados de azul (`--ui-stroke-*`), radios (`--radius-scalar: .2`, radios pequeños) |
| `context-*.js`, función que aplica el tema | Cómo se derivan esos tokens a partir de los colores base |

Los valores opacos de la variante clara son los mismos que usa el tema KDE `hermes-light-kde`
(`/mnt/datos2/Hermes/temas/kde/hermes-light-kde/tokens/computed-light.json`, calculados por Chromium),
así que el compositor y el escritorio KDE comparten paleta.

## Qué cambia

| Elemento | Claro | Oscuro |
|---|---|---|
| Fondo del canvas | Papel `#f8f9fe` con nubes `#f3f7ff`, grano sutil y rejilla de puntos de dos niveles en azul tintado; los puntos mantienen su tamaño en pantalla con cualquier zoom y los niveles se desvanecen al alejar | Lo mismo sobre `#0d1117` |
| Origen del canvas | Un anillo azul Nous marca el (0,0), el «home» | Igual, en `#4a84fe` |
| Barra de título (ventanas SSD) | `#f3f7ff`, texto `#242528`, Noto Sans medium 10, alineado a la izquierda, 34 px | `#010409`, texto `#e6edf3` |
| Borde | 1 px `#c3d1f0`; **azul Nous** en la ventana enfocada | 1 px `#30363d`; `#4a84fe` en la enfocada |
| Radio de esquinas | 6 px (igual que el tema KDE) | 6 px |
| Contorno de otros monitores | Azul Nous al 45 % | `#4a84fe` al 45 % |
| Terminales (Alacritty, foot, kitty) | Cabecera Hermes en lugar de marco desnudo | Igual |

## Instalar

```sh
extras/themes/hermes/install.sh           # copia los shaders a ~/.config/driftwm/themes/hermes/
# y después pega el contenido de hermes-light.toml (o hermes-dark.toml) en ~/.config/driftwm/config.toml
```

La ruta `~/.config/driftwm` cambiará a `~/.config/wonderlandwm` con el PR de cambio de nombre.

## Probar sin instalar

```sh
WAYLAND_DISPLAY=wayland-1 dev/bench/theme_shots.py extras/themes/hermes/hermes-light.toml /tmp/hermes-shots
```

Abre una sesión anidada con unas ventanas y guarda capturas a varios zooms, hechas con la captura del propio compositor.

## Lo que la configuración no alcanza (necesita código)

- La **sombra** es negra al 40 % fija (`SHADOW_COLOR` en `src/config/types.rs`); Hermes usa sombras suaves de varias capas (`--shadow-nous`).
- El **botón de cerrar** es una × gruesa dibujada por el compositor; Hermes usa iconos lineales finos.
- La **barra de errores** de configuración es negro/blanco fijo (`src/render/error_bar.rs`).
- Las **tarjetas de ventanas suspendidas** usan solo `fg_color`; no tienen superficie ni acento propios.
- No hay **anillo de foco** con halo como el `--dt-ring` de Hermes: solo cambia el color del borde.

## Licencia de los recursos

No se copian imágenes de la app (`hermes.png`, `filler-bg0.jpg`, iconos): son de Nous Research y este fork es GPL.
El fondo es procedural y solo reutiliza valores de color.
