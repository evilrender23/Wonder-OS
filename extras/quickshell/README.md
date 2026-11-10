# Barra de WonderlandWM (Quickshell)

Una sola instancia de Quickshell (`qs -p ~/.config/driftwm/quickshell`) ofrece:

- **Barra superior** en cada monitor, con la estética de Hermes Agent Desktop (paleta «nous» clara y oscura).
- **Servidor de notificaciones**, que sustituye a swaync.

Las funciones son las de [Quickshell Rise](https://github.com/HANCORE-linux/quickshell-dots) (MIT) adaptadas a un
canvas infinito: los *workspaces* se convierten en **bookmarks** y Hyprland se sustituye por el IPC del compositor.

Requiere Quickshell ≥ 0.3. `extras/install.sh` lo instala todo en `~/.config/driftwm/`.

## Widgets

| id | Barra | Clic izq. | Clic der. | Rueda |
|---|---|---|---|---|
| `launcher` | ☤ | AquilesAI, el agente del escritorio, en el monitor con el foco | menú del sistema estilo Apple (AquilesAI, acerca de: ventana de créditos del proyecto, ajustes, pantallas, Spotlight, forzar salida, reposo/reiniciar/apagar, bloquear, cerrar sesión) | — (clic central: ajustes) |
| `appmenu` | menús de la app enfocada, como en macOS: nombre en negrita, los menús propios de la app si los exporta (*Archivo*, *Editar*…; ver «Menús de la app»), *Ventana* (ajustar, centrar, pantalla completa, fijar, lista de ventanas), *Lienzo* (zoom, vista general, inicio, bookmarks) y *Ayuda*; con un menú abierto, pasar el ratón por otro título cambia de menú | abre el menú | — | — |
| `privacy` | punto naranja (micrófono) / verde (cámara) mientras algo graba (solo entonces) | — | — | — |
| `capture` | cámara; durante una grabación, su tiempo en rojo | capturas (o detener la grabación) | — | elegir qué grabar |
| `spotlight` | lupa | Spotlight | — | — |
| `control` | Centro de control: Wi-Fi/red, Bluetooth, concentración (no molestar), mantener activo, brillo, sonido, reproduciendo, modo oscuro, pantallas | panel | — | — |
| `bookmarks` | un punto por bookmark (estilos *default*, *numbers*, *magic*); fuera del layout por defecto | vuela al bookmark | panel Canvas | bookmark anterior/siguiente |
| `zoom` | % de zoom | vista general (`zoom-to-fit`) | 100 % | zoom |
| `windows` | título de la ventana enfocada | panel Canvas | — (clic central la cierra) | — |
| `media` | carátula, pista y ◀ ⏯ ▶ (estilo *full*: barra de progreso) | panel con seek y selector de reproductor | panel | pista anterior/siguiente |
| `clock` | fecha y hora (locale del sistema; luna con no molestar) | Centro de notificaciones: notificaciones, calendario y tiempo | 12/24 h | — (clic central: zona horaria) |
| `weather` | Open-Meteo, sin clave | previsión de 12 h y 7 días, ciudad, unidades | refrescar | — |
| `keyboard` | distribución activa | siguiente | anterior | — |
| `tray` | StatusNotifier | activar | menú | scroll del ítem (clic central: ocultar) |
| `updates` | actualizaciones pendientes (solo si hay); con [WonderUpdate](../../apps/wonderupdate) también Flatpak y el propio escritorio | lista por origen, actualizar en terminal | comprobar | — |
| `ai` | % de cuota más alto | Claude Code, Codex y OpenCode con reinicio | refrescar | — |
| `notifications` | contador | historial, acciones, no molestar | no molestar | — |
| `idle` | taza | mantener despierto (idle-inhibit) | — | — |
| `cpu` / `memory` | uso | gráfica, hilos, temperatura y procesos (con kill) | `btop` | — |
| `network` | Wi-Fi/cable (nmcli) | redes, conectar con contraseña, olvidar | nm-connection-editor | — |
| `bluetooth` | estado | escanear, emparejar, conectar, olvidar, batería | encender/apagar | — |
| `audio` | volumen | salida, entrada y volumen por app (Pipewire) | silenciar | volumen |
| `brightness` | solo con backlight | deslizador (brightnessctl) | — | ±5 % |
| `battery` | batería o perfil de energía | estado, salud y perfil | siguiente perfil | — |
| `power` | ⏻ | bloquear, suspender, salir, reiniciar y apagar (confirmación doble) | — | — |

Por defecto, la barra muestra todos los widgets salvo `zoom`, `windows` y `keyboard`. Puedes añadirlos desde Ajustes.

**Progreso de archivos.** Mientras Wonderland Finder copia, mueve o borra, el centro de la barra muestra la operación, una barra de progreso y cuánto lleva («1,7 GB de 4 GB»). No es un widget del diseño: aparece solo, delante de lo que haya en la sección central, y desaparece al terminar. Las operaciones de menos de un tercio de segundo no llegan a mostrarse. El estado lo publica el Finder en `$XDG_RUNTIME_DIR/wonderland/finder-ops.json` (`services/FinderOps.qml` lo sigue; `bar/FileProgress.qml` lo dibuja).


## Menús de la app

Las apps que exportan su barra de menús por D-Bus (`com.canonical.dbusmenu`: las apps Qt/KDE con `QT_QPA_PLATFORMTHEME=kde`, como Dolphin, Kate, KWrite, Konsole, Gwenview u Okular) la muestran en la barra, entre el nombre de la app y *Ventana*, y dejan de dibujarla dentro de la ventana. Un submenú se despliega en el sitio al pulsarlo. La *Ayuda* de la app queda la última y añade las entradas de ayuda de WonderlandWM.

Lo hace `scripts/wonderland-appmenu` (Python + `python-gobject`), que la barra arranca sola: es el registrador `com.canonical.AppMenu.Registrar` que esas apps buscan al iniciarse, y lee el menú de la ventana enfocada.

- Solo cuenta para las apps abiertas **después** de arrancar la barra; las que ya estaban abiertas siguen con su menú dentro de la ventana hasta reiniciarlas.
- Las apps que no exportan menú (GTK, Electron, Firefox, las X11) se quedan como estaban: nombre, *Ventana*, *Lienzo* y *Ayuda*.
- Con varias ventanas de un mismo proceso, cada menú se empareja con su ventana por orden de apertura.
- Para que una app Qt conserve su menú dentro de la ventana, lánzala con `KDE_NO_GLOBAL_MENU=1`.
- Comprobar qué hay: `qs -p ~/.config/driftwm/quickshell ipc call appmenu list`; abrir el menú n: `… ipc call appmenu menu <n>`.

## Spotlight (`mod+space`)

Buscador centrado en el monitor activo, al estilo de macOS (`spotlight/Spotlight.qml`). Es su propia superficie layer-shell con foco de teclado exclusivo: un popup de la barra abierto desde un atajo no tiene un serial de entrada con el que hacer grab, y por eso no recibía teclas.

- Mientras escribes, los resultados se agrupan en este orden: **Mejor resultado**, Aplicaciones, Ventanas en el lienzo (las suspendidas se relanzan), Marcadores, Sistema (pantallas, alinear, tema, bloquear, suspender, cerrar sesión, reiniciar, apagar) y Web (DuckDuckGo).
- Una expresión como `12/4` o `2^10` muestra el resultado en la **Calculadora**; Enter lo copia con `wl-copy`.
- Con ↑/↓, Tab o Ctrl+N/P te mueves por la lista, Enter abre, y Esc o un clic fuera cierran.
- La búsqueda ignora acentos y mayúsculas. Puntúa primero el prefijo, luego el inicio de palabra y luego la subcadena. Solo en los nombres se aceptan subsecuencias casi contiguas: en las listas de palabras clave, casi cualquier consulta sería subsecuencia de algo.

## Pantallas (`mod+p`, o «pantallas» en Spotlight)

Ajustes de pantallas al estilo de macOS (`displays/Displays.qml`):

- Arrastras los monitores para colocarlos y se pegan borde con borde.
- Por monitor eliges resolución, frecuencia, escala y rotación.
- **Identificar** muestra el número de cada monitor en su pantalla.

Lee la disposición real con `wlr-randr --json` (wlr-output-management). **Aplicar** escribe `[[outputs]]` en `config.toml` mediante `scripts/wonderland-displays`, que valida con `driftwm --check-config` y guarda `config.toml.displays.bak`; el compositor la recarga en caliente. Se revierte sola a los 15 s si no pulsas **Mantener**.

## Fondos de pantalla («wallpapers» o «fondos» en Spotlight)

Fondos animados y tus propias imágenes, que dibuja el propio compositor (`wallpapers/WallpaperPanel.qml`, servicio `services/Wallpapers.qml`):

- En Spotlight, «wallpapers», «fondos» o «fondo de pantalla» ofrecen **Fondos de pantalla** (abre el panel) y la lista de fondos con su miniatura; Enter aplica el elegido. Buscar por nombre («aurora», «seda») también los encuentra.
- El panel muestra la galería a la izquierda: un clic pone ese fondo en pantalla como **vista previa**. A la derecha están los ajustes del fondo elegido (colores, tamaños, formas, intensidades…) y los generales: velocidad (0 lo congela y deja de redibujarlo), desplazamiento con el lienzo (parallax), seguir el zoom y tope de fotogramas por segundo. Cada cambio se ve al momento en el escritorio, también como vista previa.
- **Aplicar** se queda con lo que estás viendo (fondo y ajustes). **Descartar**, Escape o cerrar el panel vuelven al fondo que tenías aplicado, con sus ajustes. Un ojo en la miniatura marca el fondo en vista previa; la marca de verificación, el aplicado.
- **Restablecer** pone los valores por defecto de ese fondo en la vista previa (se guardan al aplicar); **Quitar fondo animado** devuelve el `[background]` que había antes del primero.
- Un rayo en la miniatura marca los fondos exigentes con la GPU.
- **Importar imagen** abre un selector de archivos (`kdialog` o `zenity`) para añadir fotos o imágenes tuyas; se pueden elegir varias. Aparecen las primeras de la galería, con el icono de imagen, y la recién importada queda en vista previa a la espera de **Aplicar**. Sus ajustes: cómo se ajusta a la pantalla (rellenar, ajustar, estirar, centrar o mosaico), tamaño, encuadre, color de fondo, brillo, saturación y viñeta, además del desplazamiento con el lienzo y seguir el zoom. **Eliminar** borra la copia importada (no tu archivo original); si era el fondo aplicado, vuelve el `[background]` de antes.

Cada fondo es un shader de `extras/wallpapers/live/` (ver su README). `scripts/wonderland-wallpaper` genera el shader con los ajustes, lo escribe en `~/.config/driftwm/wallpapers/current.glsl`, apunta `[background]` a él (validando con `driftwm --check-config`) y guarda los ajustes de cada fondo en `~/.config/driftwm/wallpaper.json`. La vista previa usa `preview.glsl` en la misma carpeta y no toca los ajustes guardados (`wonderland-wallpaper preview ID [JSON]` / `cancel`).

Las imágenes importadas se copian a `~/.local/share/wonderlandwm/wallpapers/img-<hash>/` (`wonderland-wallpaper import ARCHIVO...`, `pick` para pedirlas con el selector, `remove ID`) y se pintan con `wallpapers/live/lib/image.glsl`, un shader de textura (`[background] texture`). El compositor solo lee PNG y JPEG: con ImageMagick instalado también se aceptan WebP, AVIF, HEIC, JPEG XL, BMP, GIF y TIFF, las fotos se enderezan según su orientación EXIF y las de más de 3840 px de lado se reducen.

## Dock

Dock al estilo de macOS en la parte de abajo de cada pantalla (`dock/Dock.qml`). De izquierda a derecha:

- **Launchpad**, que abre la parrilla de aplicaciones (ver abajo). Con clic derecho abre Spotlight.
- Las apps **fijadas**.
- Separadas por una línea, las apps **abiertas** que no están fijadas.
- La **Papelera**, que cambia de icono cuando tiene algo.

Debajo de cada app abierta hay un punto: azul si tiene el foco y apagado si todas sus ventanas están suspendidas. Al lanzar una app, su icono rebota.

**Minimizar** (como en macOS): el «–» de la barra de título, el botón de minimizar de la propia app, `Super+H` o «Minimizar» en el menú del Dock. La ventana vuela al Dock y deja una miniatura entre la línea y la Papelera; sigue abierta y vuelve al mismo sitio y con el foco al pulsarla (o `Super+Shift+H` para la última). Con `dockMinimizeIntoApp` no hay miniatura: se recupera desde el icono de la app.

- **Clic:** lleva la cámara a la ventana de la app. Si ya tiene el foco, pasa a su siguiente ventana; si todas están minimizadas, restaura la última; si todas están suspendidas, relanza una; si no está abierta, la lanza.
- **Clic central:** abre una ventana nueva.
- **Clic derecho:** lista de sus ventanas, «Nueva ventana», «Mantener en el Dock» / «Quitar del Dock», «Minimizar» y «Cerrar». En una miniatura: «Restaurar» y «Cerrar».

Las ventanas se agrupan por su `.desktop` (`DesktopEntries.heuristicLookup`). Si eso no encuentra nada, como en `com.nousresearch.hermes` ↔ `hermes-desktop`, compara el último tramo del app_id con el nombre de la app.

Ajustes (panel de ajustes → Dock, o en `bar.json`):

| Clave | Por defecto |
|---|---|
| `dockEnabled` | `true` |
| `dockAutohide` | `false`: el dock reserva su franja con una zona exclusiva. Con `true` se esconde y vuelve al tocar el borde inferior |
| `dockMinimizeIntoApp` | `false`: cada ventana minimizada tiene su miniatura junto a la Papelera. Con `true`, se minimiza dentro del icono de la app |
| `dockAllScreens` | `true`. Con `false`, solo aparece en la pantalla del origen del layout |
| `dockSize` | `48` (40/48/60/72 desde el panel) |
| `dockPinned` / `dockPinsCustomized` | ids de `.desktop` en orden. Mientras no se edite la lista, se fijan las de serie que estén instaladas |

IPC: `qs ipc call dock toggle` y `qs ipc call dock toggleAutohide`.

## Menú contextual (clic derecho)

`contextmenu/ContextMenu.qml` dibuja un menú donde la aplicación no ofrece el suyo. El compositor entrega siempre el clic derecho a la app y solo ejecuta `[mouse] context_menu` si esta no responde: no abre un popup ni una ventana y no repinta la superficie pulsada en `context_menu_delay` ms (150 por defecto). En el lienzo vacío y en la barra de título aparece al instante. Un clic derecho con un binding propio, un arrastre o una pulsación larga no lo abren.

El contenido se arma en cada apertura según lo pulsado:

| Dónde | Qué ofrece |
|---|---|
| Ventana | lo seleccionado (abrir enlace, escribir a un correo, abrir una ruta, copiar un color, resultado de una cuenta, buscar, copiar), nueva ventana y acciones del `.desktop` de la app, y las acciones de ventana |
| Barra de título | acciones de ventana y de la app |
| Lienzo | con los iconos del escritorio activos: nueva carpeta, nuevo documento de texto, ordenar iconos y abrir la carpeta Escritorio; además terminal, Spotlight, navegación (solo lo que aplica: zoom si no está al 100 %, alinear si hay varias pantallas), ventanas a las que saltar, fondos, pantallas, tema y ajustes |

El portapapeles solo añade una fila cuando contiene algo que se puede abrir. «No mostrar este menú en esta app» lo silencia para apps que responden al clic derecho sin que el compositor lo note; se revierte desde el menú del lienzo. El interruptor general está en Ajustes. Necesita `wl-clipboard`.

## Iconos del escritorio

La carpeta Escritorio (`xdg-user-dir DESKTOP`) como iconos en el monitor principal (`desktop/Desktop.qml`), con colocación libre al estilo de macOS: un icono se queda donde lo sueltas, sin rejilla.

- **Colocación libre** (por defecto): el icono se queda exactamente donde lo sueltas, sin encajes ni recolocaciones, también encima de otro.
- **Imán** (opcional, apagado por defecto; se activa en Ajustes o con clic derecho): al arrastrar, los iconos cercanos y los márgenes de la pantalla atraen al que llevas, hacia su fila o columna o a una celda de distancia. La atracción crece de forma continua al acercarse, una línea guía marca la alineación y, si sueltas dentro del radio de captura, el icono encaja; fuera de él se queda exactamente donde estaba. Con **Mayús** pulsada el arrastre es libre. Los iconos pueden quedar tan juntos como quieras; solo el que cae justo encima de otro se aparta lo mínimo para no taparlo.
- Clic selecciona, **Ctrl+clic** añade o quita de la selección y el arrastre mueve la selección entera. Doble clic o Enter abre; **Espacio** abre la vista rápida del Finder (`apps/finder`) con lo seleccionado, y otro Espacio la cierra; F2 renombra; Supr envía a la papelera; Ctrl+A selecciona todo.
- Clic derecho sobre un icono: abrir, mostrar en el gestor de archivos, renombrar, papelera, nueva carpeta, **Ordenar iconos** (cada uno a la celda libre más cercana de una rejilla que empieza arriba a la derecha) y activar o desactivar el imán.
- Los archivos nuevos ocupan la primera celda libre desde la esquina superior derecha. Las imágenes se muestran con su miniatura y los `.desktop` con su nombre e icono.

La superficie cubre la pantalla por debajo de las ventanas, pero solo los iconos reciben entrada: el resto del lienzo conserva sus clics y arrastres. `scripts/wonderland-desktop` lista y vigila la carpeta (inotify) y hace las operaciones de archivo. Las posiciones se guardan en `~/.local/state/wonderlandwm/desktop.json` tal como las dejaste: un archivo que desaparece y vuelve recupera su sitio, y una pantalla más pequeña solo recoloca los iconos mientras dure; los interruptores están en Ajustes → Desktop.

## Capturas (`Impr Pant`, el widget de la cámara, o «captura» en Spotlight)

Un overlay con una barra para elegir **qué** (ventana, área o pantalla) y **cómo** (imagen o vídeo). Un clic o un arrastre capturan; no hay paso de confirmación.

- **Ventana**: la que está bajo el puntero se ilumina; el clic la captura sola, con su sombra y fondo transparente, aunque otra la tape.
- **Área**: arrastra. Las medidas se ven mientras arrastras.
- **Pantalla**: clic en un monitor (o `Enter`) y se captura solo ese.
- **Vídeo**: lo mismo, pero graba. Una píldora con el tiempo queda fuera del área grabada; un clic en ella, en el widget o `mod+shift+r` detiene.

| Tecla | |
|---|---|
| `W` `A` `S` | ventana, área, pantalla |
| `Espacio` | alterna ventana ↔ área |
| `R` | imagen ↔ vídeo |
| `T` | temporizador: sin espera, 3, 5, 10 s |
| `C` | incluir el puntero |
| `Esc`, clic derecho | cerrar |

La imagen va a `Imágenes/Capturas` y al portapapeles (o solo al portapapeles); el vídeo a `Vídeos/Grabaciones`. Al terminar, una miniatura en la esquina permite abrir, copiar, ver en la carpeta o borrar. Las opciones se guardan en `~/.local/state/wonderlandwm/capture.json`.

El trabajo lo hace `scripts/wonderland-capture` (`grim`, la captura de ventana del compositor, `wf-recorder`; sin este último graba con `grim` + `ffmpeg` a menos fotogramas). Se puede usar solo: `wonderland-capture shot screen DP-1`, `record start area "0,0 800x600"`, `record stop`.

## Launchpad

Todas las aplicaciones instaladas en una parrilla de iconos a pantalla completa, por páginas, al estilo de macOS (`launchpad/Launchpad.qml`). Se abre con el primer icono del Dock o con `qs -p ~/.config/driftwm/quickshell ipc call launchpad toggle`, en el monitor activo.

- Escribe para filtrar por nombre.
- Flechas para moverte, **Intro** abre la app seleccionada.
- Rueda del ratón, arrastrar, **Re Pág / Av Pág** o los puntos de abajo cambian de página.
- **Esc** borra la búsqueda y, si ya está vacía, cierra. Un clic en el fondo también cierra.

Con la regla `[[window_rules]] app_id = "wonderland-launchpad"` + `blur = true` (viene en `extras/config.toml`) el compositor desenfoca el escritorio detrás.

## Ajustes (clic central en la marca, su menú, o `mod+,`)

- **Tema**:
  - Tarjetas para Hermes claro y oscuro.
  - Carrusel de fondos con las imágenes de `Imágenes/` (o de `wallpaperDir`), más el shader del tema.
  - Con «Follow with the compositor» activado, los cambios de tema y de fondo se aplican también al compositor mediante `scripts/wonderland-theme`, y driftwm los recarga en caliente.
- **Editar layout**:
  - Cada widget muestra un asa: arrástralo dentro de su sección o a otra.
  - La × lo oculta; los ocultos se vuelven a añadir con «+ id».
  - «↺» restaura el layout por defecto.
- **Estilo por widget**: relleno (none/tint/solid), borde y modo compacto (solo icono).
- **Reducir animaciones.**
- **Cristal** («Glass»): con «Liquid glass» activado, la barra, el dock, Spotlight y los paneles se vuelven translúcidos sobre la escarcha del compositor. El deslizador va de «Clear» a «Solid» (`glassOpacity`, de 0.3 a 1). Cada superficie translúcida necesita una regla `blur = true` en `config.toml`; si no, el escritorio se ve detrás sin desenfocar. Las que además tienen `corner_radius` (Spotlight, Pantallas) reciben el borde curvo y el brillo de `[effects.glass]` (cristal líquido, a partir de FluidGlass de React Bits). Los temas hermes esmerilan también todas las ventanas y dejan las que no tienen el foco al 93 %.

El estado se guarda en `~/.local/state/wonderlandwm/bar.json` con escritura atómica. Si editas el archivo a mano, la barra lo recarga.

## IPC y atajos

```sh
qs -p ~/.config/driftwm/quickshell ipc call bar togglePanel <id>   # cualquier id de la tabla, o "settings"
qs -p ~/.config/driftwm/quickshell ipc call bar toggleTheme
qs -p ~/.config/driftwm/quickshell ipc call bar setTheme dark
qs -p ~/.config/driftwm/quickshell ipc call bar toggleLayoutLock
qs -p ~/.config/driftwm/quickshell ipc call bar resetLayout
qs -p ~/.config/driftwm/quickshell ipc call bar setBookmarkStyle magic
qs -p ~/.config/driftwm/quickshell ipc call wallpapers toggle         # panel de fondos
qs -p ~/.config/driftwm/quickshell ipc call wallpapers apply aurora   # aplica un fondo por id
qs -p ~/.config/driftwm/quickshell ipc call wallpapers preview aurora # abre el panel con ese fondo en vista previa
qs -p ~/.config/driftwm/quickshell ipc call wallpapers keep           # aplica la vista previa (discard la descarta)
qs -p ~/.config/driftwm/quickshell ipc call wallpapers next           # el siguiente de la lista
qs -p ~/.config/driftwm/quickshell ipc call contextmenu open canvas 400 300 DP-1 ""   # menú contextual
qs -p ~/.config/driftwm/quickshell ipc call desktop cleanUp           # ordena los iconos del escritorio
qs -p ~/.config/driftwm/quickshell ipc call desktop toggleMagnet
qs -p ~/.config/driftwm/quickshell ipc call desktop newFolder
qs -p ~/.config/driftwm/quickshell ipc call desktop openFolder
qs -p ~/.config/driftwm/quickshell ipc call desktop toggle            # muestra u oculta los iconos
qs -p ~/.config/driftwm/quickshell ipc call capture open last         # overlay de captura: last | window | area | screen | record
qs -p ~/.config/driftwm/quickshell ipc call capture shot screen       # sin overlay: screen (la activa) o window (la enfocada)
qs -p ~/.config/driftwm/quickshell ipc call capture record            # elegir qué grabar, o detener la grabación
qs -p ~/.config/driftwm/quickshell ipc call wallpapers importImage    # abre el panel y pide imágenes que importar
```

Atajos que define `extras/config.toml`:

| Atajo | Acción |
|---|---|
| `mod+space` | Spotlight |
| `mod+a` | AquilesAI, en el monitor con el foco (sustituye a `home-toggle`, que sigue en el pellizco de 4 dedos) |
| `mod+p` | pantallas |
| `mod+n` | notificaciones |
| `mod+,` | ajustes |
| `mod+shift+b` | bookmarks |
| `mod+shift+t` | cambiar entre tema claro y oscuro |
| `Impr Pant` | capturas, en el último modo usado |
| `shift+Impr Pant`, `mod+shift+s` | capturar un área |
| `ctrl+Impr Pant` | capturar una ventana |
| `alt+Impr Pant` | esta pantalla, ya |
| `mod+shift+r` | grabar / detener la grabación |

Un panel abierto por IPC no captura el puntero: Wayland exige un evento de entrada reciente para hacerlo. Se cierra con su widget o con el mismo atajo.

## Uso de IA

El widget `ai` lee `~/.cache/{claude,codex,opencode}-usage.json`. Esos archivos los generan los scripts de Rise, que están en
`scripts/ai-usage/`:

```sh
sh ~/.config/driftwm/scripts/ai-usage/install.sh claude codex   # solo las herramientas que uses
```

Cada script se comunica únicamente con el servicio de su herramienta y usa el login que esa herramienta ya tiene guardado.
Un timer de usuario de systemd los ejecuta cada 5 minutos.

## Estructura

```
shell.qml          ShellRoot: Bar por pantalla + Toasts + IpcHandler "bar"
Theme.qml          paleta Hermes (p.*), tipografía, radios; sigue Settings.data.themeMode
services/          singletons: Settings, Compositor (IPC de driftwm), Time, Net, Brightness,
                   SysStats, Weather, Media, Notifs, AiUsage, Updates, Session, Wallpapers, Capture
spotlight/         Spotlight
contextmenu/       menú contextual de reserva
displays/          ajustes de pantallas
desktop/           iconos del escritorio (Desktop, DesktopIcon, DesktopMenu), su imán (magnet.js) e icons/ (carpeta y documento, los mismos que dibuja Wonderland Finder)
capture/           overlay de capturas y grabación (CaptureOverlay)
wallpapers/        panel de fondos de pantalla (WallpaperPanel, ColorEditor)
bar/               Bar, BarSection (arrastrar y soltar), WidgetHost, widgets/*Widget.qml
components/        Pill, BarPanel, IconButton, ListItem, Switch, TextBox, Tooltip, Icon, …
panels/            CanvasPanel, SettingsPanel, Toasts
assets/icons/      Lucide (ISC), coloreados en runtime
```

El singleton IPC se llama `Compositor` y no `Canvas` porque `Canvas` ya es un tipo de QtQuick y lo taparía.

## Pruebas

`dev/bench/qs_session.py` arranca un WonderlandWM anidado con estado y configuración temporales, lanza esta barra, abre
paneles por IPC (`--then "qs:bar togglePanel audio"`) y captura la salida real con `drift-shot` (wlr-screencopy, incluye
las capas). Opciones útiles:

- `--theme dark`
- `--ai-sample`: cachés de cuota de ejemplo.
- `--notify "título|cuerpo|critical"`: arranca qs en un D-Bus privado y envía notificaciones de prueba.
- `--bar-json '{…}'`: estado inicial de la barra.

Al terminar, el script lista las líneas problemáticas del log de qs.

`dev/scripts/test-desktop-magnet` comprueba con node la colocación de los iconos del escritorio (`desktop/magnet.js`): continuidad de la atracción, captura al soltar, que ningún icono caiga sobre otro, y la rejilla de «Ordenar iconos».
