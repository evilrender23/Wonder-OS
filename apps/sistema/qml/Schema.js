.pragma library

// Every page of the app. A setting is a config.toml key plus how to show it; its default is not
// written here, it is read from the compositor's config.reference.toml.
//
// Types: bool, int, float, text, choice, color, file, lines (a list of strings, one per line),
// flags (a list of strings picked from `options`).
// `startup` marks the keys the compositor only reads when it starts.
//
// A page with `shortcuts` or `folders` instead of groups is drawn by a component of its own;
// «Carpetas» is not in config.toml at all (see src/appearance.h). Their `keywords` are what the
// search (Search.js) finds them by, besides the title.
//
// The texts are written in Spanish and translated here, as the file is read: ids, keys and
// option values are never translated, only what is shown (and searched).

// qsTr is QML's: a bare JS engine (the tests) has none and reads the texts as they are written.
if (typeof qsTr === "undefined")
    (function () { return this })().qsTr = text => text

function s(key, type, label, desc, extra) {
    return Object.assign({ path: key.split("."), type: type, label: label, desc: desc || "" }, extra || {})
}

const placements = [
    ["center", qsTr("Centro")], ["top", qsTr("Arriba")], ["bottom", qsTr("Abajo")], ["left", qsTr("Izquierda")], ["right", qsTr("Derecha")],
    ["top-left", qsTr("Arriba a la izquierda")], ["top-right", qsTr("Arriba a la derecha")],
    ["bottom-left", qsTr("Abajo a la izquierda")], ["bottom-right", qsTr("Abajo a la derecha")]
]

const contexts = [
    ["on-window", qsTr("Sobre una ventana")],
    ["on-canvas", qsTr("Sobre el lienzo vacío")],
    ["anywhere", qsTr("En cualquier sitio")]
]

function bindingTables(section, keyLabel, keyHint) {
    return contexts.map(c => ({
        title: c[1],
        bindings: { table: [section, c[0]], keyLabel: keyLabel, keyHint: keyHint, valueLabel: qsTr("Acción"), valueHint: "pan-viewport" }
    }))
}

const pages = [
    {
        id: "general", title: qsTr("General"), icon: "◐",
        groups: [
            {
                title: qsTr("Foco y ventanas nuevas"),
                items: [
                    s("mod_key", "choice", qsTr("Tecla modificadora"), qsTr("La tecla «Mod» de los atajos del escritorio. Para Mod3 hay que asignarle antes una tecla en la distribución de teclado."),
                      { options: [["super", "Super (❖)"], ["alt", "Alt"], ["mod3", "Mod3"]] }),
                    s("focus_follows_mouse", "bool", qsTr("El foco sigue al puntero"), qsTr("La ventana bajo el puntero recibe el teclado sin hacer clic.")),
                    s("focus_placement", "choice", qsTr("Dónde se coloca la ventana activa"), qsTr("El punto de la pantalla al que la navegación lleva una ventana."), { options: placements }),
                    s("window_placement", "choice", qsTr("Dónde se abren las ventanas"),
                      qsTr("Automático: junto a la ventana activa. Centro: en el punto de arriba, y la cámara la sigue. Cursor: bajo el puntero."),
                      { options: [["auto", qsTr("Automático")], ["center", qsTr("Centro")], ["cursor", qsTr("Bajo el cursor")]] })
                ]
            },
            {
                title: qsTr("Sesión"),
                note: qsTr("El lienzo se guarda al salir y vuelve en el siguiente inicio."),
                items: [
                    s("session.suspend_on_close", "bool", qsTr("Suspender en vez de cerrar"), qsTr("Al cerrar una ventana desde la propia app queda un marcador en el lienzo para reabrirla.")),
                    s("session.restore_windows", "bool", qsTr("Recordar las ventanas abiertas"), qsTr("Las ventanas abiertas al salir vuelven como marcadores suspendidos.")),
                    s("session.restore_camera", "bool", qsTr("Recordar la cámara y el zoom")),
                    s("session.restore_bookmarks", "bool", qsTr("Recordar los marcadores"), qsTr("Conserva los marcadores cambiados durante la sesión."))
                ]
            },
            {
                title: qsTr("Programas de inicio"),
                items: [
                    s("autostart", "lines", qsTr("Órdenes al iniciar"), qsTr("Una orden por línea; cada una se ejecuta con sh -c al arrancar el escritorio."), { startup: true, hint: "waybar" })
                ]
            }
        ]
    },
    {
        id: "teclado", title: qsTr("Teclado"), icon: "⌨",
        groups: [
            {
                title: qsTr("Distribución"),
                items: [
                    s("input.keyboard.layout", "text", qsTr("Distribución"), qsTr("Código XKB. Varias separadas por comas: «es,us»."), { hint: "es" }),
                    s("input.keyboard.variant", "text", qsTr("Variante"), qsTr("Por ejemplo «dvorak»; con varias distribuciones, una por cada una separadas por comas."), { hint: "nodeadkeys" }),
                    s("input.keyboard.options", "text", qsTr("Opciones XKB"), qsTr("Por ejemplo «grp:win_space_toggle» o «caps:escape»."), { hint: "caps:escape" }),
                    s("input.keyboard.model", "text", qsTr("Modelo"), "", { hint: "pc105" }),
                    s("input.keyboard.remember_layout_per_window", "bool", qsTr("Recordar la distribución de cada ventana")),
                    s("input.keyboard.layout_independent", "bool", qsTr("Atajos por tecla física"), qsTr("Los atajos responden a la misma tecla en cualquier distribución."))
                ]
            },
            {
                title: qsTr("Repetición y bloqueos"),
                items: [
                    s("input.keyboard.repeat_rate", "int", qsTr("Velocidad de repetición"), qsTr("Teclas por segundo."), { min: 1, max: 100, unit: "/s" }),
                    s("input.keyboard.repeat_delay", "int", qsTr("Retardo antes de repetir"), "", { min: 100, max: 2000, step: 10, unit: "ms" }),
                    s("input.keyboard.debounce", "int", qsTr("Filtro de doble pulsación"), qsTr("Ignora una tecla que se repite sola justo después de soltarla (letras dobles por un interruptor gastado). 0 lo desactiva; entre 30 y 60 ms va bien en casi cualquier teclado."), { min: 0, max: 200, step: 5, unit: "ms" }),
                    s("input.keyboard.num_lock", "bool", qsTr("Bloq Num activado al iniciar"), "", { startup: true }),
                    s("input.keyboard.caps_lock", "bool", qsTr("Bloq Mayús activado al iniciar"), "", { startup: true })
                ]
            }
        ]
    },
    { id: "atajos", title: qsTr("Atajos de teclado"), icon: "⌘", shortcuts: true, keywords: qsTr("keybindings combinación") },
    {
        id: "raton", title: qsTr("Ratón y panel táctil"), icon: "⌖",
        groups: [
            {
                title: qsTr("Ratón"),
                items: [
                    s("input.mouse.accel_speed", "float", qsTr("Velocidad del puntero"), "", { min: -1, max: 1, step: 0.05 }),
                    s("input.mouse.accel_profile", "choice", qsTr("Aceleración"), "", { options: [["flat", qsTr("Plana")], ["adaptive", qsTr("Adaptativa")]] }),
                    s("input.mouse.natural_scroll", "bool", qsTr("Desplazamiento natural"), qsTr("El contenido sigue a la rueda.")),
                    s("input.mouse.left_handed", "bool", qsTr("Modo zurdo"), qsTr("Intercambia los botones izquierdo y derecho."))
                ]
            },
            {
                title: qsTr("Panel táctil"),
                items: [
                    s("input.trackpad.enable", "bool", qsTr("Panel táctil activado")),
                    s("input.trackpad.disable_on_external_mouse", "bool", qsTr("Desactivarlo con un ratón conectado")),
                    s("input.trackpad.tap_to_click", "bool", qsTr("Tocar para hacer clic")),
                    s("input.trackpad.tap_and_drag", "bool", qsTr("Tocar y arrastrar"), qsTr("Doble toque manteniendo el dedo para arrastrar.")),
                    s("input.trackpad.natural_scroll", "bool", qsTr("Desplazamiento natural"), qsTr("El contenido sigue a los dedos.")),
                    s("input.trackpad.disable_while_typing", "bool", qsTr("Ignorarlo mientras se escribe")),
                    s("input.trackpad.accel_speed", "float", qsTr("Velocidad del puntero"), "", { min: -1, max: 1, step: 0.05 }),
                    s("input.trackpad.accel_profile", "choice", qsTr("Aceleración"), "", { options: [["flat", qsTr("Plana")], ["adaptive", qsTr("Adaptativa")]] }),
                    s("input.trackpad.click_method", "choice", qsTr("Clic secundario"), "",
                      { options: [["none", qsTr("El del dispositivo")], ["clickfinger", qsTr("Según el número de dedos")], ["button_areas", qsTr("Según la zona pulsada")]] })
                ]
            },
            {
                title: qsTr("Cursor"),
                items: [
                    s("cursor.theme", "text", qsTr("Tema del cursor"), qsTr("«none» usa el del entorno."), { hint: "Adwaita" }),
                    s("cursor.size", "int", qsTr("Tamaño"), qsTr("0 usa el del entorno."), { min: 0, max: 256, unit: "px" }),
                    s("cursor.inactive_opacity", "float", qsTr("Opacidad en las otras pantallas"), "", { min: 0, max: 1, step: 0.05 })
                ]
            },
            {
                title: qsTr("Ventanas con el ratón"),
                items: [
                    s("mouse.resize_on_border", "bool", qsTr("Redimensionar arrastrando el borde")),
                    s("mouse.decoration_resize_snapped", "bool", qsTr("El borde arrastra las ventanas acopladas"), qsTr("Al redimensionar desde el borde se mueve también el grupo acoplado.")),
                    s("mouse.decoration_fit_snapped", "bool", qsTr("Maximizar lleva las ventanas acopladas"), qsTr("Al maximizar desde la ventana o la barra de tareas."))
                ]
            }
        ]
    },
    {
        id: "navegacion", title: qsTr("Lienzo y zoom"), icon: "✥",
        groups: [
            {
                title: qsTr("Desplazamiento"),
                items: [
                    s("navigation.trackpad_speed", "float", qsTr("Velocidad con el panel táctil"), qsTr("1 = directo, 0 = desactivado."), { min: 0, max: 5, step: 0.1 }),
                    s("navigation.mouse_speed", "float", qsTr("Velocidad con el ratón"), "", { min: 0, max: 5, step: 0.1 }),
                    s("navigation.touch_speed", "float", qsTr("Velocidad con la pantalla táctil"), "", { min: 0, max: 5, step: 0.1 }),
                    s("navigation.drift", "float", qsTr("Inercia"), qsTr("0 = sin inercia, 1 = la más suelta."), { min: 0, max: 1, step: 0.05 }),
                    s("navigation.camera_speed", "float", qsTr("Rapidez de la cámara"), qsTr("1 = salto instantáneo."), { min: 0.05, max: 1, step: 0.05 }),
                    s("navigation.pan_step", "float", qsTr("Paso de «mover la vista»"), "", { min: 1, max: 2000, step: 10, unit: "px" }),
                    s("navigation.nudge_step", "int", qsTr("Paso de «empujar ventana»"), "", { min: 1, max: 500, unit: "px" }),
                    s("navigation.resize_step", "int", qsTr("Paso de «agrandar / encoger»"), "", { min: 1, max: 500, unit: "px" }),
                    s("navigation.auto_navigate_on_close", "bool", qsTr("Ir a la siguiente ventana al cerrar"), qsTr("Si la que recibe el foco está fuera de la pantalla, la cámara va a ella.")),
                    s("navigation.auto_navigate_on_click", "bool", qsTr("Traer a la vista al hacer clic"), qsTr("Un clic en una ventana a medias fuera de la pantalla la desplaza hasta verse entera."))
                ]
            },
            {
                title: qsTr("Desplazar desde los bordes"),
                note: qsTr("Al arrastrar una ventana hasta el borde de la pantalla, el lienzo se desplaza."),
                items: [
                    s("navigation.edge_pan.zone", "float", qsTr("Anchura de la zona"), "", { min: 0, max: 500, step: 5, unit: "px" }),
                    s("navigation.edge_pan.speed_min", "float", qsTr("Velocidad mínima"), "", { min: 0, max: 100, step: 0.5, unit: qsTr("px/fotograma") }),
                    s("navigation.edge_pan.speed_max", "float", qsTr("Velocidad máxima"), "", { min: 0, max: 100, step: 0.5, unit: qsTr("px/fotograma") }),
                    s("navigation.edge_pan.cursor_pan", "bool", qsTr("También con el cursor solo"), qsTr("Desplaza al tocar el borde sin arrastrar nada.")),
                    s("navigation.edge_pan.cursor_zone", "float", qsTr("Zona del cursor solo"), "", { min: 1, max: 200, step: 1, unit: "px" }),
                    s("navigation.edge_pan.latency_ms", "int", qsTr("Retardo en bordes entre pantallas"), "", { min: 0, max: 2000, step: 10, unit: "ms" })
                ]
            },
            {
                title: qsTr("Zoom"),
                items: [
                    s("zoom.step", "float", qsTr("Paso por pulsación"), qsTr("1,1 = 10 % cada vez."), { min: 1.01, max: 2, step: 0.01 }),
                    s("zoom.trackpad_speed", "float", qsTr("Velocidad con el panel táctil"), "", { min: 0, max: 5, step: 0.1 }),
                    s("zoom.mouse_speed", "float", qsTr("Velocidad con la rueda"), "", { min: 0, max: 5, step: 0.1 }),
                    s("zoom.touch_speed", "float", qsTr("Velocidad con la pantalla táctil"), "", { min: 0, max: 5, step: 0.1 }),
                    s("zoom.fit_padding", "float", qsTr("Margen de «ver todo»"), "", { min: 0, max: 500, step: 5, unit: "px" }),
                    s("zoom.reset_on_new_window", "bool", qsTr("Volver al 100 % al abrir una ventana")),
                    s("zoom.reset_on_activation", "bool", qsTr("Volver al 100 % cuando una ventana pide el foco")),
                    s("zoom.interact_min", "float", qsTr("Zoom mínimo para usar las ventanas"), qsTr("Por debajo, un clic va a la ventana y arrastrar la mueve. 0 lo desactiva."), { min: 0, max: 1, step: 0.05 })
                ]
            }
        ]
    },
    {
        id: "ventanas", title: qsTr("Ventanas"), icon: "▣",
        groups: [
            {
                title: qsTr("Acoplado"),
                items: [
                    s("snap.enabled", "bool", qsTr("Acoplar ventanas al arrastrar")),
                    s("snap.gap", "float", qsTr("Separación entre ventanas"), "", { min: 0, max: 200, step: 1, unit: "px" }),
                    s("snap.outer_gap", "float", qsTr("Margen con el borde de la pantalla"), "", { min: 0, max: 200, step: 1, unit: "px" }),
                    s("snap.distance", "float", qsTr("Distancia a la que atrae"), "", { min: 0, max: 200, step: 1, unit: "px" }),
                    s("snap.break_force", "float", qsTr("Fuerza para soltarse"), "", { min: 0, max: 300, step: 1, unit: "px" }),
                    s("snap.corners", "bool", qsTr("Alinear también bordes paralelos")),
                    s("snap.centers", "bool", qsTr("Alinear también los centros"))
                ]
            },
            {
                title: qsTr("Decoración"),
                note: qsTr("El tema del escritorio (claro/oscuro) reescribe los colores de este grupo al cambiar."),
                items: [
                    s("decorations.default_mode", "choice", qsTr("Barra de título"), qsTr("Para las apps que ya dibujan la suya conviene «La de la app»."),
                      { options: [["client", qsTr("La de la app")], ["minimal", qsTr("Sin barra: sombra, esquinas y borde")], ["none", qsTr("Nada")]] }),
                    s("decorations.corner_radius", "int", qsTr("Radio de las esquinas"), "", { min: 0, max: 60, unit: "px" }),
                    s("decorations.shadow", "bool", qsTr("Sombra")),
                    s("decorations.border_width", "int", qsTr("Grosor del borde"), qsTr("0 = sin borde."), { min: 0, max: 20, unit: "px" }),
                    s("decorations.border_color", "color", qsTr("Color del borde")),
                    s("decorations.border_color_focused", "color", qsTr("Color del borde de la ventana activa")),
                    s("decorations.opacity", "float", qsTr("Opacidad de las ventanas inactivas"), "", { min: 0.1, max: 1, step: 0.05 }),
                    s("decorations.opacity_focused", "float", qsTr("Opacidad de la ventana activa"), "", { min: 0.1, max: 1, step: 0.05 }),
                    s("decorations.blur", "bool", qsTr("Desenfocar el fondo de las ventanas"))
                ]
            },
            {
                title: qsTr("Barra de título del escritorio"),
                note: qsTr("Para las ventanas a las que una regla les pone la barra del escritorio."),
                items: [
                    s("decorations.bg_color", "color", qsTr("Fondo")),
                    s("decorations.fg_color", "color", qsTr("Texto y botón de cerrar")),
                    s("decorations.title_bar_height", "int", qsTr("Altura"), "", { min: 10, max: 80, unit: "px" }),
                    s("decorations.font", "text", qsTr("Tipografía"), "", { hint: "Adwaita Sans" }),
                    s("decorations.font_size", "int", qsTr("Tamaño del texto"), "", { min: 6, max: 40, unit: "pt" }),
                    s("decorations.font_weight", "choice", qsTr("Grosor del texto"), "",
                      { options: [["thin", qsTr("Fina")], ["extralight", qsTr("Extraligera")], ["light", qsTr("Ligera")], ["regular", qsTr("Normal")], ["medium", qsTr("Media")],
                                  ["semibold", qsTr("Seminegrita")], ["bold", qsTr("Negrita")], ["extrabold", qsTr("Extranegrita")], ["black", qsTr("Negra")]] }),
                    s("decorations.title_align", "choice", qsTr("Alineación del título"), "", { options: [["left", qsTr("Izquierda")], ["center", qsTr("Centro")]] })
                ]
            }
        ]
    },
    {
        id: "efectos", title: qsTr("Efectos"), icon: "✦",
        groups: [
            {
                title: qsTr("Animaciones"),
                items: [
                    s("effects.animation_speed", "float", qsTr("Rapidez"), qsTr("1 = sin animación."), { min: 0.05, max: 1, step: 0.05 }),
                    s("effects.animation_scale", "float", qsTr("Escala al abrir y cerrar"), qsTr("1 = solo fundido."), { min: 0.5, max: 1, step: 0.01 })
                ]
            },
            {
                title: qsTr("Desenfoque"),
                items: [
                    s("effects.blur_radius", "int", qsTr("Pasadas"), "", { min: 1, max: 10 }),
                    s("effects.blur_strength", "float", qsTr("Fuerza"), "", { min: 0.1, max: 10, step: 0.1 }),
                    s("effects.animate_blur_fps", "int", qsTr("Refresco sobre fondos animados"), qsTr("0 lo congela."), { min: 0, max: 144, unit: "fps" })
                ]
            },
            {
                title: qsTr("Cristal líquido"),
                note: qsTr("Sobre las superficies desenfocadas: el borde curva y descompone lo que hay detrás."),
                items: [
                    s("effects.glass.enabled", "bool", qsTr("Cristal líquido")),
                    s("effects.glass.refraction", "float", qsTr("Refracción"), "", { min: 0, max: 2, step: 0.05 }),
                    s("effects.glass.dispersion", "float", qsTr("Dispersión del color"), "", { min: 0, max: 1, step: 0.05 }),
                    s("effects.glass.bevel", "int", qsTr("Anchura del borde"), "", { min: 0, max: 100, unit: "px" }),
                    s("effects.glass.reflection", "float", qsTr("Reflejo del borde"), "", { min: 0, max: 2, step: 0.05 }),
                    s("effects.glass.tint", "color", qsTr("Tinte")),
                    s("effects.glass.tint_strength", "float", qsTr("Intensidad del tinte"), "", { min: 0, max: 1, step: 0.05 })
                ]
            },
            {
                title: qsTr("Desenfoque de movimiento del cursor"),
                note: qsTr("Al mover el puntero deprisa deja una estela suave que se desvanece al parar. En reposo no dibuja nada."),
                items: [
                    s("effects.cursor_blur.enabled", "bool", qsTr("Desenfoque de movimiento del cursor")),
                    s("effects.cursor_blur.strength", "float", qsTr("Intensidad"), qsTr("Longitud y opacidad de la estela."), { min: 0, max: 2, step: 0.05 })
                ]
            },
            {
                title: qsTr("Ventanas gelatinosas"),
                note: qsTr("Al arrastrar una ventana se comba como gelatina tras el puntero y se asienta con un rebote al soltarla."),
                items: [
                    s("effects.wobbly.enabled", "bool", qsTr("Ventanas gelatinosas")),
                    s("effects.wobbly.stiffness", "float", qsTr("Rigidez"), qsTr("Más alta: sigue al puntero más de cerca."), { min: 1, max: 20, step: 0.5 }),
                    s("effects.wobbly.friction", "float", qsTr("Fricción"), qsTr("Más baja: tiembla durante más tiempo."), { min: 0.5, max: 20, step: 0.5 }),
                    s("effects.wobbly.exclude", "lines", qsTr("Apps excluidas"), qsTr("Un app_id por línea; esas ventanas se mueven rígidas."), { hint: "mpv" })
                ]
            },
            {
                title: qsTr("Ventanas 3D"),
                note: qsTr("Al arrastrar una ventana se inclina en perspectiva hacia donde la llevas, como una carta sujeta por un punto, y vuelve a quedar plana con un pequeño vaivén al parar. Se combina con las ventanas gelatinosas."),
                items: [
                    s("effects.tilt.enabled", "bool", qsTr("Ventanas 3D")),
                    s("effects.tilt.strength", "float", qsTr("Inclinación"), qsTr("Cuánto se ladea: 1 son unos 20° a toda velocidad."), { min: 0.2, max: 2, step: 0.1 }),
                    s("effects.tilt.shine", "bool", qsTr("Luz y reflejo"), qsTr("Sombrea el lado que se aleja y cruza un destello por la ventana.")),
                    s("effects.tilt.exclude", "lines", qsTr("Apps excluidas"), qsTr("Un app_id por línea; esas ventanas se mueven planas."), { hint: "mpv" })
                ]
            },
            {
                title: qsTr("Lámpara mágica al minimizar"),
                note: qsTr("La ventana se escurre por un embudo hasta el dock y vuelve a salir de él al restaurarla. Desactivada, vuela en línea recta."),
                items: [
                    s("effects.magic_lamp.enabled", "bool", qsTr("Lámpara mágica al minimizar")),
                    s("effects.magic_lamp.duration_ms", "int", qsTr("Duración"), "", { min: 50, max: 2000, step: 10, unit: "ms" })
                ]
            },
            {
                title: qsTr("Animación al cerrar"),
                note: qsTr("Cómo desaparece una ventana al cerrarla, al estilo de Burn My Windows. Minimizar conserva su propia animación."),
                items: [
                    s("effects.close_animation.style", "choice", qsTr("Efecto"), qsTr("«Aleatorio» elige uno distinto para cada ventana."),
                      { options: [["fade", qsTr("Desvanecer")], ["fire", qsTr("Fuego")], ["incinerate", qsTr("Incinerar")], ["tv", qsTr("Televisor antiguo")],
                                  ["glide", qsTr("Deslizar")], ["pixelate", qsTr("Pixelar")], ["hexagon", qsTr("Hexágonos")], ["energize", qsTr("Teletransporte")],
                                  ["matrix", "Matrix"], ["doom", qsTr("Derretir (Doom)")], ["apparition", qsTr("Aparición")], ["random", qsTr("Aleatorio")]] }),
                    s("effects.close_animation.duration_ms", "int", qsTr("Duración"), qsTr("Duración base; el fuego dura algo más y el televisor algo menos. No afecta a «Desvanecer»."), { min: 100, max: 3000, step: 10, unit: "ms" })
                ]
            }
        ]
    },
    { id: "carpetas", title: qsTr("Carpetas"), icon: "▰", folders: true, keywords: qsTr("color carpeta tinte escritorio rabbit folder_tint") },
    {
        id: "fondo", title: qsTr("Fondo", "page"), icon: "▨",
        groups: [
            {
                title: qsTr("Fondo del lienzo"),
                note: qsTr("El tema del escritorio reescribe este grupo al cambiar entre claro y oscuro."),
                items: [
                    s("background.type", "choice", qsTr("Tipo"), "",
                      { options: [["default", qsTr("Rejilla de puntos")], ["wallpaper", qsTr("Imagen fija a la pantalla")], ["tile", qsTr("Imagen en mosaico")],
                                  ["shader", qsTr("Shader GLSL")], ["none", qsTr("Ninguno (lo pone otro programa)")]] }),
                    s("background.path", "file", qsTr("Archivo"), qsTr("La imagen o el shader, según el tipo.")),
                    s("background.texture", "file", qsTr("Textura del shader"), qsTr("Imagen que el shader puede muestrear.")),
                    s("background.mirror_tile", "bool", qsTr("Mosaico en espejo"), qsTr("Evita las costuras de una imagen que no es continua.")),
                    s("background.cache_shader", "bool", qsTr("Dibujar el shader una sola vez"), qsTr("Para shaders estáticos: se dibuja una vez y se desplaza el resultado.")),
                    s("background.transparent_shader", "bool", qsTr("Respetar la transparencia del shader"), qsTr("Deja ver un fondo de otro programa debajo.")),
                    s("background.cache_budget_mb", "int", qsTr("Memoria para el fondo"), "", { min: 16, max: 4096, step: 16, unit: "MB" }),
                    s("background.animate_fps", "int", qsTr("Límite de fotogramas del shader"), qsTr("0 = todos."), { min: 0, max: 1000, unit: "fps" })
                ]
            }
        ]
    },
    {
        id: "pantallas", title: qsTr("Pantallas"), icon: "▭",
        groups: [
            {
                title: qsTr("Varias pantallas"),
                note: qsTr("La resolución, la escala y la posición de cada monitor se ajustan en «Pantallas…», en el menú del escritorio."),
                items: [
                    s("output.contiguous", "bool", qsTr("Lienzo continuo entre pantallas"), qsTr("Cada monitor empieza junto a los demás en vez de mirar al mismo punto.")),
                    s("output.outline.thickness", "int", qsTr("Contorno de las otras pantallas"), qsTr("Marca en el lienzo lo que ven los demás monitores. 0 lo oculta."), { min: 0, max: 20, unit: "px" }),
                    s("output.outline.color", "color", qsTr("Color del contorno")),
                    s("output.outline.opacity", "float", qsTr("Opacidad del contorno"), "", { min: 0, max: 1, step: 0.05 })
                ]
            },
            {
                title: qsTr("Pantalla táctil y tableta"),
                items: [
                    s("input.touch.enable", "bool", qsTr("Pantalla táctil activada")),
                    s("input.touch.map_to_output", "text", qsTr("Monitor de la pantalla táctil"), qsTr("Nombre del conector; «none» lo detecta solo."), { hint: "DP-2" }),
                    s("input.tablet.map_to_output", "text", qsTr("Monitor de la tableta"), "", { hint: "DP-2" })
                ]
            }
        ]
    },
    {
        id: "gestos", title: qsTr("Gestos y ratón"), icon: "≋",
        groups: [
            {
                title: qsTr("Gestos del panel táctil"),
                items: [
                    s("gestures.swipe_threshold", "float", qsTr("Recorrido para un deslizamiento"), "", { min: 1, max: 200, step: 1, unit: "px" }),
                    s("gestures.pinch_in_threshold", "float", qsTr("Umbral de pellizco hacia dentro"), "", { min: 0.1, max: 0.99, step: 0.01 }),
                    s("gestures.pinch_out_threshold", "float", qsTr("Umbral de pellizco hacia fuera"), "", { min: 1.01, max: 3, step: 0.01 })
                ]
            }
        ].concat(bindingTables("gestures", qsTr("Gesto"), "mod+3-finger-swipe")).concat([
            { title: qsTr("Botones y rueda del ratón"), note: qsTr("Disparadores: left, right, middle, wheel-scroll, wheel-up, wheel-down, trackpad-scroll."), items: [] }
        ]).concat(bindingTables("mouse", qsTr("Botón"), "alt+left")).concat([
            {
                title: qsTr("Pantalla táctil"),
                items: [
                    s("touch.swipe_threshold", "float", qsTr("Recorrido para un deslizamiento"), "", { min: 1, max: 100, step: 0.5, unit: "mm" }),
                    s("touch.pinch_in_threshold", "float", qsTr("Umbral de pellizco hacia dentro"), "", { min: 0.1, max: 0.99, step: 0.01 }),
                    s("touch.pinch_out_threshold", "float", qsTr("Umbral de pellizco hacia fuera"), "", { min: 1.01, max: 3, step: 0.01 }),
                    s("touch.tap_time", "int", qsTr("Duración máxima de un toque"), "", { min: 50, max: 2000, step: 10, unit: "ms" }),
                    s("touch.double_tap_time", "int", qsTr("Tiempo para el doble toque"), "", { min: 50, max: 2000, step: 10, unit: "ms" }),
                    s("touch.hold_time", "int", qsTr("Tiempo para mantener pulsado"), "", { min: 50, max: 3000, step: 10, unit: "ms" }),
                    s("touch.tap_travel", "float", qsTr("Movimiento permitido en un toque"), "", { min: 0, max: 20, step: 0.5, unit: "mm" })
                ]
            }
        ]).concat(bindingTables("touch", qsTr("Gesto"), "3-finger-tap"))
    },
    {
        id: "avanzado", title: qsTr("Avanzado"), icon: "⚙",
        groups: [
            {
                title: qsTr("Atajos de fábrica"),
                items: [
                    s("bindings.disable_defaults", "flags", qsTr("Quitar todos los de fábrica de"), qsTr("Quedan solo los tuyos en cada categoría marcada."),
                      { options: [["keys", qsTr("Teclado")], ["mouse", qsTr("Ratón")], ["gestures", qsTr("Gestos")], ["touch", qsTr("Táctil")]] })
                ]
            },
            {
                title: qsTr("Aplicaciones X11"),
                items: [
                    s("xwayland.enabled", "bool", qsTr("Iniciar xwayland-satellite"), "", { startup: true }),
                    s("xwayland.path", "text", qsTr("Ruta de xwayland-satellite"), "", { startup: true, hint: "xwayland-satellite" })
                ]
            },
            {
                title: qsTr("Gráficos"),
                note: qsTr("Apaños para parpadeos o fallos de dibujo, sobre todo con NVIDIA."),
                items: [
                    s("backend.wait_for_frame_completion", "bool", qsTr("Esperar a la GPU antes de cada fotograma"), "", { startup: true }),
                    s("backend.disable_direct_scanout", "bool", qsTr("Componer siempre con EGL"), "", { startup: true }),
                    s("backend.disable_hardware_cursor", "bool", qsTr("Dibujar el cursor por software"), qsTr("Arregla un cursor a tirones."), { startup: true }),
                    s("backend.max_capture_fps", "int", qsTr("Límite al grabar la pantalla"), qsTr("0 = sin límite."), { startup: true, min: 0, max: 240, unit: "fps" })
                ]
            },
            {
                title: qsTr("Variables de entorno"),
                note: qsTr("Se fijan antes de arrancar cualquier programa; cambian al reiniciar la sesión."),
                bindings: { table: ["env"], keyLabel: qsTr("Variable"), keyHint: "QT_SCALE_FACTOR", valueLabel: qsTr("Valor"), valueHint: "1", plain: true }
            }
        ]
    }
]
