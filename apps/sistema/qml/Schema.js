.pragma library

// Every page of the app. A setting is a config.toml key plus how to show it; its default is not
// written here, it is read from the compositor's config.reference.toml.
//
// Types: bool, int, float, text, choice, color, file, lines (a list of strings, one per line),
// flags (a list of strings picked from `options`).
// `startup` marks the keys the compositor only reads when it starts.
//
// A page with `shortcuts` or `folders` instead of groups is drawn by a component of its own;
// «Carpetas» is not in config.toml at all (see src/appearance.h).

function s(key, type, label, desc, extra) {
    return Object.assign({ path: key.split("."), type: type, label: label, desc: desc || "" }, extra || {})
}

const placements = [
    ["center", "Centro"], ["top", "Arriba"], ["bottom", "Abajo"], ["left", "Izquierda"], ["right", "Derecha"],
    ["top-left", "Arriba a la izquierda"], ["top-right", "Arriba a la derecha"],
    ["bottom-left", "Abajo a la izquierda"], ["bottom-right", "Abajo a la derecha"]
]

const contexts = [
    ["on-window", "Sobre una ventana"],
    ["on-canvas", "Sobre el lienzo vacío"],
    ["anywhere", "En cualquier sitio"]
]

function bindingTables(section, keyLabel, keyHint) {
    return contexts.map(c => ({
        title: c[1],
        bindings: { table: [section, c[0]], keyLabel: keyLabel, keyHint: keyHint, valueLabel: "Acción", valueHint: "pan-viewport" }
    }))
}

const pages = [
    {
        id: "general", title: "General", icon: "◐",
        groups: [
            {
                title: "Foco y ventanas nuevas",
                items: [
                    s("mod_key", "choice", "Tecla modificadora", "La tecla «Mod» de los atajos del escritorio. Para Mod3 hay que asignarle antes una tecla en la distribución de teclado.",
                      { options: [["super", "Super (❖)"], ["alt", "Alt"], ["mod3", "Mod3"]] }),
                    s("focus_follows_mouse", "bool", "El foco sigue al puntero", "La ventana bajo el puntero recibe el teclado sin hacer clic."),
                    s("focus_placement", "choice", "Dónde se coloca la ventana activa", "El punto de la pantalla al que la navegación lleva una ventana.", { options: placements }),
                    s("window_placement", "choice", "Dónde se abren las ventanas",
                      "Automático: junto a la ventana activa. Centro: en el punto de arriba, y la cámara la sigue. Cursor: bajo el puntero.",
                      { options: [["auto", "Automático"], ["center", "Centro"], ["cursor", "Bajo el cursor"]] })
                ]
            },
            {
                title: "Sesión",
                note: "El lienzo se guarda al salir y vuelve en el siguiente inicio.",
                items: [
                    s("session.suspend_on_close", "bool", "Suspender en vez de cerrar", "Al cerrar una ventana desde la propia app queda un marcador en el lienzo para reabrirla."),
                    s("session.restore_windows", "bool", "Recordar las ventanas abiertas", "Las ventanas abiertas al salir vuelven como marcadores suspendidos."),
                    s("session.restore_camera", "bool", "Recordar la cámara y el zoom"),
                    s("session.restore_bookmarks", "bool", "Recordar los marcadores", "Conserva los marcadores cambiados durante la sesión.")
                ]
            },
            {
                title: "Programas de inicio",
                items: [
                    s("autostart", "lines", "Órdenes al iniciar", "Una orden por línea; cada una se ejecuta con sh -c al arrancar el escritorio.", { startup: true, hint: "waybar" })
                ]
            }
        ]
    },
    {
        id: "teclado", title: "Teclado", icon: "⌨",
        groups: [
            {
                title: "Distribución",
                items: [
                    s("input.keyboard.layout", "text", "Distribución", "Código XKB. Varias separadas por comas: «es,us».", { hint: "es" }),
                    s("input.keyboard.variant", "text", "Variante", "Por ejemplo «dvorak»; con varias distribuciones, una por cada una separadas por comas.", { hint: "nodeadkeys" }),
                    s("input.keyboard.options", "text", "Opciones XKB", "Por ejemplo «grp:win_space_toggle» o «caps:escape».", { hint: "caps:escape" }),
                    s("input.keyboard.model", "text", "Modelo", "", { hint: "pc105" }),
                    s("input.keyboard.remember_layout_per_window", "bool", "Recordar la distribución de cada ventana"),
                    s("input.keyboard.layout_independent", "bool", "Atajos por tecla física", "Los atajos responden a la misma tecla en cualquier distribución.")
                ]
            },
            {
                title: "Repetición y bloqueos",
                items: [
                    s("input.keyboard.repeat_rate", "int", "Velocidad de repetición", "Teclas por segundo.", { min: 1, max: 100, unit: "/s" }),
                    s("input.keyboard.repeat_delay", "int", "Retardo antes de repetir", "", { min: 100, max: 2000, step: 10, unit: "ms" }),
                    s("input.keyboard.num_lock", "bool", "Bloq Num activado al iniciar", "", { startup: true }),
                    s("input.keyboard.caps_lock", "bool", "Bloq Mayús activado al iniciar", "", { startup: true })
                ]
            }
        ]
    },
    { id: "atajos", title: "Atajos de teclado", icon: "⌘", shortcuts: true },
    {
        id: "raton", title: "Ratón y panel táctil", icon: "⌖",
        groups: [
            {
                title: "Ratón",
                items: [
                    s("input.mouse.accel_speed", "float", "Velocidad del puntero", "", { min: -1, max: 1, step: 0.05 }),
                    s("input.mouse.accel_profile", "choice", "Aceleración", "", { options: [["flat", "Plana"], ["adaptive", "Adaptativa"]] }),
                    s("input.mouse.natural_scroll", "bool", "Desplazamiento natural", "El contenido sigue a la rueda."),
                    s("input.mouse.left_handed", "bool", "Modo zurdo", "Intercambia los botones izquierdo y derecho.")
                ]
            },
            {
                title: "Panel táctil",
                items: [
                    s("input.trackpad.enable", "bool", "Panel táctil activado"),
                    s("input.trackpad.disable_on_external_mouse", "bool", "Desactivarlo con un ratón conectado"),
                    s("input.trackpad.tap_to_click", "bool", "Tocar para hacer clic"),
                    s("input.trackpad.tap_and_drag", "bool", "Tocar y arrastrar", "Doble toque manteniendo el dedo para arrastrar."),
                    s("input.trackpad.natural_scroll", "bool", "Desplazamiento natural", "El contenido sigue a los dedos."),
                    s("input.trackpad.disable_while_typing", "bool", "Ignorarlo mientras se escribe"),
                    s("input.trackpad.accel_speed", "float", "Velocidad del puntero", "", { min: -1, max: 1, step: 0.05 }),
                    s("input.trackpad.accel_profile", "choice", "Aceleración", "", { options: [["flat", "Plana"], ["adaptive", "Adaptativa"]] }),
                    s("input.trackpad.click_method", "choice", "Clic secundario", "",
                      { options: [["none", "El del dispositivo"], ["clickfinger", "Según el número de dedos"], ["button_areas", "Según la zona pulsada"]] })
                ]
            },
            {
                title: "Cursor",
                items: [
                    s("cursor.theme", "text", "Tema del cursor", "«none» usa el del entorno.", { hint: "Adwaita" }),
                    s("cursor.size", "int", "Tamaño", "0 usa el del entorno.", { min: 0, max: 256, unit: "px" }),
                    s("cursor.inactive_opacity", "float", "Opacidad en las otras pantallas", "", { min: 0, max: 1, step: 0.05 })
                ]
            },
            {
                title: "Ventanas con el ratón",
                items: [
                    s("mouse.resize_on_border", "bool", "Redimensionar arrastrando el borde"),
                    s("mouse.decoration_resize_snapped", "bool", "El borde arrastra las ventanas acopladas", "Al redimensionar desde el borde se mueve también el grupo acoplado."),
                    s("mouse.decoration_fit_snapped", "bool", "Maximizar lleva las ventanas acopladas", "Al maximizar desde la ventana o la barra de tareas.")
                ]
            }
        ]
    },
    {
        id: "navegacion", title: "Lienzo y zoom", icon: "✥",
        groups: [
            {
                title: "Desplazamiento",
                items: [
                    s("navigation.trackpad_speed", "float", "Velocidad con el panel táctil", "1 = directo, 0 = desactivado.", { min: 0, max: 5, step: 0.1 }),
                    s("navigation.mouse_speed", "float", "Velocidad con el ratón", "", { min: 0, max: 5, step: 0.1 }),
                    s("navigation.touch_speed", "float", "Velocidad con la pantalla táctil", "", { min: 0, max: 5, step: 0.1 }),
                    s("navigation.drift", "float", "Inercia", "0 = sin inercia, 1 = la más suelta.", { min: 0, max: 1, step: 0.05 }),
                    s("navigation.camera_speed", "float", "Rapidez de la cámara", "1 = salto instantáneo.", { min: 0.05, max: 1, step: 0.05 }),
                    s("navigation.pan_step", "float", "Paso de «mover la vista»", "", { min: 1, max: 2000, step: 10, unit: "px" }),
                    s("navigation.nudge_step", "int", "Paso de «empujar ventana»", "", { min: 1, max: 500, unit: "px" }),
                    s("navigation.resize_step", "int", "Paso de «agrandar / encoger»", "", { min: 1, max: 500, unit: "px" }),
                    s("navigation.auto_navigate_on_close", "bool", "Ir a la siguiente ventana al cerrar", "Si la que recibe el foco está fuera de la pantalla, la cámara va a ella."),
                    s("navigation.auto_navigate_on_click", "bool", "Traer a la vista al hacer clic", "Un clic en una ventana a medias fuera de la pantalla la desplaza hasta verse entera.")
                ]
            },
            {
                title: "Desplazar desde los bordes",
                note: "Al arrastrar una ventana hasta el borde de la pantalla, el lienzo se desplaza.",
                items: [
                    s("navigation.edge_pan.zone", "float", "Anchura de la zona", "", { min: 0, max: 500, step: 5, unit: "px" }),
                    s("navigation.edge_pan.speed_min", "float", "Velocidad mínima", "", { min: 0, max: 100, step: 0.5, unit: "px/fotograma" }),
                    s("navigation.edge_pan.speed_max", "float", "Velocidad máxima", "", { min: 0, max: 100, step: 0.5, unit: "px/fotograma" }),
                    s("navigation.edge_pan.cursor_pan", "bool", "También con el cursor solo", "Desplaza al tocar el borde sin arrastrar nada."),
                    s("navigation.edge_pan.cursor_zone", "float", "Zona del cursor solo", "", { min: 1, max: 200, step: 1, unit: "px" }),
                    s("navigation.edge_pan.latency_ms", "int", "Retardo en bordes entre pantallas", "", { min: 0, max: 2000, step: 10, unit: "ms" })
                ]
            },
            {
                title: "Zoom",
                items: [
                    s("zoom.step", "float", "Paso por pulsación", "1,1 = 10 % cada vez.", { min: 1.01, max: 2, step: 0.01 }),
                    s("zoom.trackpad_speed", "float", "Velocidad con el panel táctil", "", { min: 0, max: 5, step: 0.1 }),
                    s("zoom.mouse_speed", "float", "Velocidad con la rueda", "", { min: 0, max: 5, step: 0.1 }),
                    s("zoom.touch_speed", "float", "Velocidad con la pantalla táctil", "", { min: 0, max: 5, step: 0.1 }),
                    s("zoom.fit_padding", "float", "Margen de «ver todo»", "", { min: 0, max: 500, step: 5, unit: "px" }),
                    s("zoom.reset_on_new_window", "bool", "Volver al 100 % al abrir una ventana"),
                    s("zoom.reset_on_activation", "bool", "Volver al 100 % cuando una ventana pide el foco"),
                    s("zoom.interact_min", "float", "Zoom mínimo para usar las ventanas", "Por debajo, un clic va a la ventana y arrastrar la mueve. 0 lo desactiva.", { min: 0, max: 1, step: 0.05 })
                ]
            }
        ]
    },
    {
        id: "ventanas", title: "Ventanas", icon: "▣",
        groups: [
            {
                title: "Acoplado",
                items: [
                    s("snap.enabled", "bool", "Acoplar ventanas al arrastrar"),
                    s("snap.gap", "float", "Separación entre ventanas", "", { min: 0, max: 200, step: 1, unit: "px" }),
                    s("snap.outer_gap", "float", "Margen con el borde de la pantalla", "", { min: 0, max: 200, step: 1, unit: "px" }),
                    s("snap.distance", "float", "Distancia a la que atrae", "", { min: 0, max: 200, step: 1, unit: "px" }),
                    s("snap.break_force", "float", "Fuerza para soltarse", "", { min: 0, max: 300, step: 1, unit: "px" }),
                    s("snap.corners", "bool", "Alinear también bordes paralelos"),
                    s("snap.centers", "bool", "Alinear también los centros")
                ]
            },
            {
                title: "Decoración",
                note: "El tema del escritorio (claro/oscuro) reescribe los colores de este grupo al cambiar.",
                items: [
                    s("decorations.default_mode", "choice", "Barra de título", "Para las apps que ya dibujan la suya conviene «La de la app».",
                      { options: [["client", "La de la app"], ["minimal", "Sin barra: sombra, esquinas y borde"], ["none", "Nada"]] }),
                    s("decorations.corner_radius", "int", "Radio de las esquinas", "", { min: 0, max: 60, unit: "px" }),
                    s("decorations.shadow", "bool", "Sombra"),
                    s("decorations.border_width", "int", "Grosor del borde", "0 = sin borde.", { min: 0, max: 20, unit: "px" }),
                    s("decorations.border_color", "color", "Color del borde"),
                    s("decorations.border_color_focused", "color", "Color del borde de la ventana activa"),
                    s("decorations.opacity", "float", "Opacidad de las ventanas inactivas", "", { min: 0.1, max: 1, step: 0.05 }),
                    s("decorations.opacity_focused", "float", "Opacidad de la ventana activa", "", { min: 0.1, max: 1, step: 0.05 }),
                    s("decorations.blur", "bool", "Desenfocar el fondo de las ventanas")
                ]
            },
            {
                title: "Barra de título del escritorio",
                note: "Para las ventanas a las que una regla les pone la barra del escritorio.",
                items: [
                    s("decorations.bg_color", "color", "Fondo"),
                    s("decorations.fg_color", "color", "Texto y botón de cerrar"),
                    s("decorations.title_bar_height", "int", "Altura", "", { min: 10, max: 80, unit: "px" }),
                    s("decorations.font", "text", "Tipografía", "", { hint: "Adwaita Sans" }),
                    s("decorations.font_size", "int", "Tamaño del texto", "", { min: 6, max: 40, unit: "pt" }),
                    s("decorations.font_weight", "choice", "Grosor del texto", "",
                      { options: [["thin", "Fina"], ["extralight", "Extraligera"], ["light", "Ligera"], ["regular", "Normal"], ["medium", "Media"],
                                  ["semibold", "Seminegrita"], ["bold", "Negrita"], ["extrabold", "Extranegrita"], ["black", "Negra"]] }),
                    s("decorations.title_align", "choice", "Alineación del título", "", { options: [["left", "Izquierda"], ["center", "Centro"]] })
                ]
            }
        ]
    },
    {
        id: "efectos", title: "Efectos", icon: "✦",
        groups: [
            {
                title: "Animaciones",
                items: [
                    s("effects.animation_speed", "float", "Rapidez", "1 = sin animación.", { min: 0.05, max: 1, step: 0.05 }),
                    s("effects.animation_scale", "float", "Escala al abrir y cerrar", "1 = solo fundido.", { min: 0.5, max: 1, step: 0.01 })
                ]
            },
            {
                title: "Desenfoque",
                items: [
                    s("effects.blur_radius", "int", "Pasadas", "", { min: 1, max: 10 }),
                    s("effects.blur_strength", "float", "Fuerza", "", { min: 0.1, max: 10, step: 0.1 }),
                    s("effects.animate_blur_fps", "int", "Refresco sobre fondos animados", "0 lo congela.", { min: 0, max: 144, unit: "fps" })
                ]
            },
            {
                title: "Cristal líquido",
                note: "Sobre las superficies desenfocadas: el borde curva y descompone lo que hay detrás.",
                items: [
                    s("effects.glass.enabled", "bool", "Cristal líquido"),
                    s("effects.glass.refraction", "float", "Refracción", "", { min: 0, max: 2, step: 0.05 }),
                    s("effects.glass.dispersion", "float", "Dispersión del color", "", { min: 0, max: 1, step: 0.05 }),
                    s("effects.glass.bevel", "int", "Anchura del borde", "", { min: 0, max: 100, unit: "px" }),
                    s("effects.glass.reflection", "float", "Reflejo del borde", "", { min: 0, max: 2, step: 0.05 }),
                    s("effects.glass.tint", "color", "Tinte"),
                    s("effects.glass.tint_strength", "float", "Intensidad del tinte", "", { min: 0, max: 1, step: 0.05 })
                ]
            },
            {
                title: "Desenfoque de movimiento del cursor",
                note: "Al mover el puntero deprisa deja una estela suave que se desvanece al parar. En reposo no dibuja nada.",
                items: [
                    s("effects.cursor_blur.enabled", "bool", "Desenfoque de movimiento del cursor"),
                    s("effects.cursor_blur.strength", "float", "Intensidad", "Longitud y opacidad de la estela.", { min: 0, max: 2, step: 0.05 })
                ]
            },
            {
                title: "Ventanas gelatinosas",
                note: "Al arrastrar una ventana se comba como gelatina tras el puntero y se asienta con un rebote al soltarla.",
                items: [
                    s("effects.wobbly.enabled", "bool", "Ventanas gelatinosas"),
                    s("effects.wobbly.stiffness", "float", "Rigidez", "Más alta: sigue al puntero más de cerca.", { min: 1, max: 20, step: 0.5 }),
                    s("effects.wobbly.friction", "float", "Fricción", "Más baja: tiembla durante más tiempo.", { min: 0.5, max: 20, step: 0.5 }),
                    s("effects.wobbly.exclude", "lines", "Apps excluidas", "Un app_id por línea; esas ventanas se mueven rígidas.", { hint: "mpv" })
                ]
            },
            {
                title: "Lámpara mágica al minimizar",
                note: "La ventana se escurre por un embudo hasta el dock y vuelve a salir de él al restaurarla. Desactivada, vuela en línea recta.",
                items: [
                    s("effects.magic_lamp.enabled", "bool", "Lámpara mágica al minimizar"),
                    s("effects.magic_lamp.duration_ms", "int", "Duración", "", { min: 50, max: 2000, step: 10, unit: "ms" })
                ]
            }
        ]
    },
    { id: "carpetas", title: "Carpetas", icon: "▰", folders: true },
    {
        id: "fondo", title: "Fondo", icon: "▨",
        groups: [
            {
                title: "Fondo del lienzo",
                note: "El tema del escritorio reescribe este grupo al cambiar entre claro y oscuro.",
                items: [
                    s("background.type", "choice", "Tipo", "",
                      { options: [["default", "Rejilla de puntos"], ["wallpaper", "Imagen fija a la pantalla"], ["tile", "Imagen en mosaico"],
                                  ["shader", "Shader GLSL"], ["none", "Ninguno (lo pone otro programa)"]] }),
                    s("background.path", "file", "Archivo", "La imagen o el shader, según el tipo."),
                    s("background.texture", "file", "Textura del shader", "Imagen que el shader puede muestrear."),
                    s("background.mirror_tile", "bool", "Mosaico en espejo", "Evita las costuras de una imagen que no es continua."),
                    s("background.cache_shader", "bool", "Dibujar el shader una sola vez", "Para shaders estáticos: se dibuja una vez y se desplaza el resultado."),
                    s("background.transparent_shader", "bool", "Respetar la transparencia del shader", "Deja ver un fondo de otro programa debajo."),
                    s("background.cache_budget_mb", "int", "Memoria para el fondo", "", { min: 16, max: 4096, step: 16, unit: "MB" }),
                    s("background.animate_fps", "int", "Límite de fotogramas del shader", "0 = todos.", { min: 0, max: 1000, unit: "fps" })
                ]
            }
        ]
    },
    {
        id: "pantallas", title: "Pantallas", icon: "▭",
        groups: [
            {
                title: "Varias pantallas",
                note: "La resolución, la escala y la posición de cada monitor se ajustan en «Pantallas…», en el menú del escritorio.",
                items: [
                    s("output.contiguous", "bool", "Lienzo continuo entre pantallas", "Cada monitor empieza junto a los demás en vez de mirar al mismo punto."),
                    s("output.outline.thickness", "int", "Contorno de las otras pantallas", "Marca en el lienzo lo que ven los demás monitores. 0 lo oculta.", { min: 0, max: 20, unit: "px" }),
                    s("output.outline.color", "color", "Color del contorno"),
                    s("output.outline.opacity", "float", "Opacidad del contorno", "", { min: 0, max: 1, step: 0.05 })
                ]
            },
            {
                title: "Pantalla táctil y tableta",
                items: [
                    s("input.touch.enable", "bool", "Pantalla táctil activada"),
                    s("input.touch.map_to_output", "text", "Monitor de la pantalla táctil", "Nombre del conector; «none» lo detecta solo.", { hint: "DP-2" }),
                    s("input.tablet.map_to_output", "text", "Monitor de la tableta", "", { hint: "DP-2" })
                ]
            }
        ]
    },
    {
        id: "gestos", title: "Gestos y ratón", icon: "≋",
        groups: [
            {
                title: "Gestos del panel táctil",
                items: [
                    s("gestures.swipe_threshold", "float", "Recorrido para un deslizamiento", "", { min: 1, max: 200, step: 1, unit: "px" }),
                    s("gestures.pinch_in_threshold", "float", "Umbral de pellizco hacia dentro", "", { min: 0.1, max: 0.99, step: 0.01 }),
                    s("gestures.pinch_out_threshold", "float", "Umbral de pellizco hacia fuera", "", { min: 1.01, max: 3, step: 0.01 })
                ]
            }
        ].concat(bindingTables("gestures", "Gesto", "mod+3-finger-swipe")).concat([
            { title: "Botones y rueda del ratón", note: "Disparadores: left, right, middle, wheel-scroll, wheel-up, wheel-down, trackpad-scroll.", items: [] }
        ]).concat(bindingTables("mouse", "Botón", "alt+left")).concat([
            {
                title: "Pantalla táctil",
                items: [
                    s("touch.swipe_threshold", "float", "Recorrido para un deslizamiento", "", { min: 1, max: 100, step: 0.5, unit: "mm" }),
                    s("touch.pinch_in_threshold", "float", "Umbral de pellizco hacia dentro", "", { min: 0.1, max: 0.99, step: 0.01 }),
                    s("touch.pinch_out_threshold", "float", "Umbral de pellizco hacia fuera", "", { min: 1.01, max: 3, step: 0.01 }),
                    s("touch.tap_time", "int", "Duración máxima de un toque", "", { min: 50, max: 2000, step: 10, unit: "ms" }),
                    s("touch.double_tap_time", "int", "Tiempo para el doble toque", "", { min: 50, max: 2000, step: 10, unit: "ms" }),
                    s("touch.hold_time", "int", "Tiempo para mantener pulsado", "", { min: 50, max: 3000, step: 10, unit: "ms" }),
                    s("touch.tap_travel", "float", "Movimiento permitido en un toque", "", { min: 0, max: 20, step: 0.5, unit: "mm" })
                ]
            }
        ]).concat(bindingTables("touch", "Gesto", "3-finger-tap"))
    },
    {
        id: "avanzado", title: "Avanzado", icon: "⚙",
        groups: [
            {
                title: "Atajos de fábrica",
                items: [
                    s("bindings.disable_defaults", "flags", "Quitar todos los de fábrica de", "Quedan solo los tuyos en cada categoría marcada.",
                      { options: [["keys", "Teclado"], ["mouse", "Ratón"], ["gestures", "Gestos"], ["touch", "Táctil"]] })
                ]
            },
            {
                title: "Aplicaciones X11",
                items: [
                    s("xwayland.enabled", "bool", "Iniciar xwayland-satellite", "", { startup: true }),
                    s("xwayland.path", "text", "Ruta de xwayland-satellite", "", { startup: true, hint: "xwayland-satellite" })
                ]
            },
            {
                title: "Gráficos",
                note: "Apaños para parpadeos o fallos de dibujo, sobre todo con NVIDIA.",
                items: [
                    s("backend.wait_for_frame_completion", "bool", "Esperar a la GPU antes de cada fotograma", "", { startup: true }),
                    s("backend.disable_direct_scanout", "bool", "Componer siempre con EGL", "", { startup: true }),
                    s("backend.disable_hardware_cursor", "bool", "Dibujar el cursor por software", "Arregla un cursor a tirones.", { startup: true }),
                    s("backend.max_capture_fps", "int", "Límite al grabar la pantalla", "0 = sin límite.", { startup: true, min: 0, max: 240, unit: "fps" })
                ]
            },
            {
                title: "Variables de entorno",
                note: "Se fijan antes de arrancar cualquier programa; cambian al reiniciar la sesión.",
                bindings: { table: ["env"], keyLabel: "Variable", keyHint: "QT_SCALE_FACTOR", valueLabel: "Valor", valueHint: "1", plain: true }
            }
        ]
    }
]
