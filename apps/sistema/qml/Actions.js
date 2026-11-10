.pragma library

// The compositor's keyboard actions (ACTION_NAMES in src/config/parse.rs), named for people.
// `arg` says what follows the action's name in a binding.

const directions = [
    ["up", qsTr("arriba")], ["down", qsTr("abajo")], ["left", qsTr("izquierda")], ["right", qsTr("derecha")],
    ["up-left", qsTr("arriba a la izquierda")], ["up-right", qsTr("arriba a la derecha")],
    ["down-left", qsTr("abajo a la izquierda")], ["down-right", qsTr("abajo a la derecha")]
]

const args = {
    direction: directions,
    cardinal: directions.slice(0, 4),
    cycle: [["forward", qsTr("siguiente")], ["backward", qsTr("anterior")]],
    trackpad: [["toggle", qsTr("alternar")], ["on", qsTr("activar")], ["off", qsTr("desactivar")]]
}

const list = [
    { id: "spawn", label: qsTr("Ejecutar una orden"), arg: "command", group: "Abrir" },
    { id: "exec", label: qsTr("Abrir una aplicación"), arg: "command", group: "Abrir", hint: qsTr("Con cursor de carga; sale de pantalla completa.") },
    { id: "exec-terminal", label: qsTr("Abrir el terminal"), group: "Abrir" },
    { id: "exec-launcher", label: qsTr("Abrir el lanzador"), group: "Abrir" },
    { id: "exec-file-manager", label: qsTr("Abrir el explorador de archivos"), group: "Abrir", hint: qsTr("El explorador predeterminado para carpetas.") },

    { id: "close-window", label: qsTr("Cerrar la ventana"), group: "Ventana" },
    { id: "suspend-window", label: qsTr("Suspender la ventana"), group: "Ventana" },
    { id: "toggle-fullscreen", label: qsTr("Pantalla completa"), group: "Ventana" },
    { id: "fit-window", label: qsTr("Maximizar"), group: "Ventana" },
    { id: "fit-window-snapped", label: qsTr("Maximizar con las acopladas"), group: "Ventana" },
    { id: "fill-window", label: qsTr("Ocupar el hueco libre"), group: "Ventana" },
    { id: "toggle-pin-to-screen", label: qsTr("Fijar a la pantalla"), group: "Ventana" },
    { id: "center-window", label: qsTr("Centrar la ventana"), group: "Ventana" },
    { id: "nudge-window", label: qsTr("Empujar la ventana"), arg: "direction", group: "Ventana" },
    { id: "move-to-snap", label: qsTr("Mover hasta el siguiente acople"), arg: "cardinal", group: "Ventana" },
    { id: "grow-window", label: qsTr("Agrandar la ventana"), arg: "direction", group: "Ventana" },
    { id: "shrink-window", label: qsTr("Encoger la ventana"), arg: "direction", group: "Ventana" },
    { id: "resize-to-snap", label: qsTr("Redimensionar hasta el siguiente acople"), arg: "cardinal", group: "Ventana" },
    { id: "send-to-output", label: qsTr("Enviar la ventana a otra pantalla"), arg: "direction", group: "Ventana" },
    { id: "move-to-bookmark", label: qsTr("Mover la ventana a un marcador"), arg: "bookmark", group: "Ventana" },

    { id: "focus-nearest", label: qsTr("Enfocar la ventana más cercana"), arg: "direction", group: "Navegar" },
    { id: "center-nearest", label: qsTr("Traer la ventana más cercana"), arg: "direction", group: "Navegar" },
    { id: "focus-center", label: qsTr("Enfocar la ventana bajo el puntero"), group: "Navegar" },
    { id: "cycle-windows", label: qsTr("Cambiar de ventana"), arg: "cycle", group: "Navegar" },
    { id: "pan-viewport", label: qsTr("Mover la vista"), arg: "direction", group: "Navegar" },
    { id: "home-toggle", label: qsTr("Ir al origen y volver"), group: "Navegar" },
    { id: "go-to-bookmark", label: qsTr("Ir a un marcador"), arg: "bookmark", group: "Navegar" },
    { id: "set-bookmark", label: qsTr("Guardar la vista en un marcador"), arg: "bookmark", group: "Navegar" },
    { id: "zoom-in", label: qsTr("Acercar"), group: "Navegar" },
    { id: "zoom-out", label: qsTr("Alejar"), group: "Navegar" },
    { id: "zoom-reset", label: qsTr("Zoom al 100 %"), group: "Navegar" },
    { id: "zoom-to-fit", label: qsTr("Ver todas las ventanas"), group: "Navegar" },
    { id: "zoom-to-fit-snapped", label: qsTr("Ver el grupo acoplado"), group: "Navegar" },
    { id: "align-outputs", label: qsTr("Alinear las vistas de las pantallas"), group: "Navegar" },
    { id: "send-cursor-to-output", label: qsTr("Llevar el cursor a otra pantalla"), arg: "direction", group: "Navegar" },

    { id: "switch-layout", label: qsTr("Cambiar la distribución de teclado"), arg: "layout", group: "Sistema" },
    { id: "set-trackpad", label: qsTr("Panel táctil"), arg: "trackpad", group: "Sistema" },
    { id: "toggle-cursor-pan", label: qsTr("Desplazar con el cursor en los bordes"), group: "Sistema" },
    { id: "reload-config", label: qsTr("Recargar la configuración"), group: "Sistema" },
    { id: "quit", label: qsTr("Salir del escritorio"), group: "Sistema" }
]

/// An action's `group`, and "Tuyos" / "Otros", are keys: the list of shortcuts sorts by them.
/// This is how each one is shown.
const groupLabels = {
    "Tuyos": qsTr("Tuyos"), "Abrir": qsTr("Abrir"), "Ventana": qsTr("Ventana"),
    "Navegar": qsTr("Navegar"), "Sistema": qsTr("Sistema"), "Otros": qsTr("Otros")
}

function groupLabel(group) {
    return groupLabels[group] || group
}

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
        return arg === "next" ? qsTr("siguiente") : (arg === "prev" ? qsTr("anterior") : qsTr("n.º %1").arg(arg))
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
