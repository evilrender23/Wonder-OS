.pragma library

// The compositor's keyboard actions (ACTION_NAMES in src/config/parse.rs), named for people.
// `arg` says what follows the action's name in a binding.

const directions = [
    ["up", "arriba"], ["down", "abajo"], ["left", "izquierda"], ["right", "derecha"],
    ["up-left", "arriba a la izquierda"], ["up-right", "arriba a la derecha"],
    ["down-left", "abajo a la izquierda"], ["down-right", "abajo a la derecha"]
]

const args = {
    direction: directions,
    cardinal: directions.slice(0, 4),
    cycle: [["forward", "siguiente"], ["backward", "anterior"]],
    trackpad: [["toggle", "alternar"], ["on", "activar"], ["off", "desactivar"]]
}

const list = [
    { id: "spawn", label: "Ejecutar una orden", arg: "command", group: "Abrir" },
    { id: "exec", label: "Abrir una aplicación", arg: "command", group: "Abrir", hint: "Con cursor de carga; sale de pantalla completa." },
    { id: "exec-terminal", label: "Abrir el terminal", group: "Abrir" },
    { id: "exec-launcher", label: "Abrir el lanzador", group: "Abrir" },
    { id: "exec-file-manager", label: "Abrir el explorador de archivos", group: "Abrir", hint: "El explorador predeterminado para carpetas." },

    { id: "close-window", label: "Cerrar la ventana", group: "Ventana" },
    { id: "suspend-window", label: "Suspender la ventana", group: "Ventana" },
    { id: "toggle-fullscreen", label: "Pantalla completa", group: "Ventana" },
    { id: "fit-window", label: "Maximizar", group: "Ventana" },
    { id: "fit-window-snapped", label: "Maximizar con las acopladas", group: "Ventana" },
    { id: "fill-window", label: "Ocupar el hueco libre", group: "Ventana" },
    { id: "toggle-pin-to-screen", label: "Fijar a la pantalla", group: "Ventana" },
    { id: "center-window", label: "Centrar la ventana", group: "Ventana" },
    { id: "nudge-window", label: "Empujar la ventana", arg: "direction", group: "Ventana" },
    { id: "move-to-snap", label: "Mover hasta el siguiente acople", arg: "cardinal", group: "Ventana" },
    { id: "grow-window", label: "Agrandar la ventana", arg: "direction", group: "Ventana" },
    { id: "shrink-window", label: "Encoger la ventana", arg: "direction", group: "Ventana" },
    { id: "resize-to-snap", label: "Redimensionar hasta el siguiente acople", arg: "cardinal", group: "Ventana" },
    { id: "send-to-output", label: "Enviar la ventana a otra pantalla", arg: "direction", group: "Ventana" },
    { id: "move-to-bookmark", label: "Mover la ventana a un marcador", arg: "bookmark", group: "Ventana" },

    { id: "focus-nearest", label: "Enfocar la ventana más cercana", arg: "direction", group: "Navegar" },
    { id: "center-nearest", label: "Traer la ventana más cercana", arg: "direction", group: "Navegar" },
    { id: "focus-center", label: "Enfocar la ventana bajo el puntero", group: "Navegar" },
    { id: "cycle-windows", label: "Cambiar de ventana", arg: "cycle", group: "Navegar" },
    { id: "pan-viewport", label: "Mover la vista", arg: "direction", group: "Navegar" },
    { id: "home-toggle", label: "Ir al origen y volver", group: "Navegar" },
    { id: "go-to-bookmark", label: "Ir a un marcador", arg: "bookmark", group: "Navegar" },
    { id: "set-bookmark", label: "Guardar la vista en un marcador", arg: "bookmark", group: "Navegar" },
    { id: "zoom-in", label: "Acercar", group: "Navegar" },
    { id: "zoom-out", label: "Alejar", group: "Navegar" },
    { id: "zoom-reset", label: "Zoom al 100 %", group: "Navegar" },
    { id: "zoom-to-fit", label: "Ver todas las ventanas", group: "Navegar" },
    { id: "zoom-to-fit-snapped", label: "Ver el grupo acoplado", group: "Navegar" },
    { id: "align-outputs", label: "Alinear las vistas de las pantallas", group: "Navegar" },
    { id: "send-cursor-to-output", label: "Llevar el cursor a otra pantalla", arg: "direction", group: "Navegar" },

    { id: "switch-layout", label: "Cambiar la distribución de teclado", arg: "layout", group: "Sistema" },
    { id: "set-trackpad", label: "Panel táctil", arg: "trackpad", group: "Sistema" },
    { id: "toggle-cursor-pan", label: "Desplazar con el cursor en los bordes", group: "Sistema" },
    { id: "reload-config", label: "Recargar la configuración", group: "Sistema" },
    { id: "quit", label: "Salir del escritorio", group: "Sistema" }
]

function find(id) {
    for (let i = 0; i < list.length; i++) {
        if (list[i].id === id)
            return list[i]
    }
    return null
}

/// A binding's value as {id, arg}.
function parse(value) {
    const text = String(value).trim()
    const space = text.search(/\s/)
    return space < 0 ? { id: text, arg: "" } : { id: text.slice(0, space), arg: text.slice(space + 1).trim() }
}

function compose(id, arg) {
    const action = find(id)
    return action && action.arg ? (id + " " + String(arg).trim()).trim() : id
}

function argLabel(kind, arg) {
    const options = args[kind]
    if (options) {
        for (let i = 0; i < options.length; i++) {
            if (options[i][0] === arg)
                return options[i][1]
        }
    }
    if (kind === "layout")
        return arg === "next" ? "siguiente" : (arg === "prev" ? "anterior" : "n.º " + arg)
    return arg
}

/// What a binding does, in words.
function describe(value) {
    const parsed = parse(value)
    const action = find(parsed.id)
    if (!action)
        return String(value)
    if (action.arg === "command")
        return parsed.arg
    return action.arg && parsed.arg ? action.label + ": " + argLabel(action.arg, parsed.arg) : action.label
}

function group(value) {
    const action = find(parse(value).id)
    return action ? action.group : "Otros"
}
