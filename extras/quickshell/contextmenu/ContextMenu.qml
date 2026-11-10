import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../services"
import "../components"
import ".."

// The desktop context menu. The compositor runs `[mouse] context_menu` only
// for a right-click on empty canvas or on a title bar; inside a window the
// right-click is the app's alone, so this never covers a menu an app shows.
//
// What it offers is rebuilt on every open from what was clicked: on a title
// bar the window, its state and the .desktop actions of its app; on the canvas
// the desktop, navigation and whatever the clipboard holds that can be opened
// (link, path, e-mail, sum or colour).
// Esc, a click outside or picking a row closes it.
Scope {
    id: root

    property bool open: false
    property var screen: null
    property string target: "canvas"       // canvas | titlebar
    property int clickX: 0
    property int clickY: 0
    property var win: null                 // the clicked window's state entry
    property string clipboard: ""
    property var items: []
    property string armed: ""              // id of the row awaiting a confirming click
    property bool desktop: false           // the shell is showing the Desktop folder as icons

    readonly property int maxText: 4000
    readonly property var terminals: ["foot", "footclient", "alacritty", "kitty", "com.mitchellh.ghostty",
        "org.wezfurlong.wezterm", "org.kde.konsole", "org.gnome.ptyxis", "org.gnome.console"]

    function show(target, x, y, output, windowId) {
        if (!Settings.data.contextMenu) return
        // Anything else (a compositor older than this shell still reports
        // clicks inside a window) is not ours to answer.
        if (target !== "canvas" && target !== "titlebar") return
        const state = Compositor.state
        const id = Number(windowId) || 0
        const w = id && state ? (state.windows || []).find(w => w.id === id) || null : null
        if (target === "titlebar" && !w) return

        root.target = target
        root.win = w
        root.clickX = x
        root.clickY = y
        root.screen = Quickshell.screens.find(s => s.name === output) || Quickshell.screens[0]
        root.clipboard = ""
        root.armed = ""
        rebuild()
        root.open = true
        if (target === "canvas") {
            desktopProbe.running = true
            clip.running = true
        }
    }

    function close() { root.open = false }

    function run(item) {
        if (item.confirm && root.armed !== item.id) { root.armed = item.id; return }
        close()
        item.run()
    }

    // ── what the clipboard text is ──────────────────────────────────────
    function ellipsis(s, n) {
        s = String(s).replace(/\s+/g, " ").trim()
        return s.length > n ? s.slice(0, n - 1) + "…" : s
    }
    function shellQuote(s) { return "'" + String(s).replace(/'/g, "'\\''") + "'" }
    function openUrl(url) { Quickshell.execDetached(["xdg-open", url]) }
    function copy(text) { Quickshell.execDetached(["wl-copy", "--", text]) }

    function asUrl(s) {
        s = s.trim()
        if (/\s/.test(s)) return ""
        if (/^(https?|ftp|file):\/\/\S+$/i.test(s)) return s
        if (/^www\.\S+\.\S+$/i.test(s)) return "https://" + s
        if (/^[a-z0-9-]+(\.[a-z0-9-]+)*\.(com|org|net|io|dev|es|app|ai|edu|gov|co|me|info|tv|uk|de|fr|mx|ar)(\/\S*)?$/i.test(s))
            return "https://" + s
        return ""
    }
    function asEmail(s) { s = s.trim(); return /^[^\s@]+@[^\s@]+\.[a-z]{2,}$/i.test(s) ? s : "" }
    function asPath(s) {
        s = s.trim()
        return /^(~|\/)[^\n]*$/.test(s) && s.length > 1 && s.length < 1024 ? s : ""
    }
    function asColor(s) { s = s.trim(); return /^#([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i.test(s) ? s : "" }
    function asSum(s) {
        s = s.trim().replace(/,/g, ".").replace(/[×x]/g, "*").replace(/÷/g, "/")
        if (s.length > 80 || !/^[\d\s+\-*/().%^]+$/.test(s) || !/\d\s*[+\-*/%^]\s*[\d(]/.test(s)) return ""
        try {
            const v = Function(`"use strict"; return (${s.replace(/\^/g, "**")})`)()
            return typeof v === "number" && isFinite(v) ? String(Math.round(v * 1e10) / 1e10) : ""
        } catch (e) { return "" }
    }

    // Rows for a clipboard text that is something to open; none otherwise.
    function clipboardItems(text) {
        const out = []
        const t = text.trim()
        if (!t) return out
        const src = I18n.tr(" del portapapeles")
        const url = asUrl(t), mail = asEmail(t), path = asPath(t), color = asColor(t), sum = asSum(t)
        if (url)
            out.push({ icon: "globe", text: I18n.tr("Abrir enlace") + src, hint: ellipsis(url.replace(/^https?:\/\//, ""), 28), run: () => openUrl(url) })
        else if (mail)
            out.push({ icon: "corner-down-left", text: I18n.tr("Escribir a %1").arg(ellipsis(mail, 30)), run: () => openUrl("mailto:" + mail) })
        else if (path) {
            // The shell expands ~ and checks the path exists before opening.
            const expand = "p=" + shellQuote(path) + "; p=\"${p/#\\~/$HOME}\"; "
            out.push({ icon: "hard-drive", text: I18n.tr("Abrir %1").arg(ellipsis(path, 34)) + src,
                run: () => Quickshell.execDetached(["bash", "-c", expand + "[ -e \"$p\" ] && xdg-open \"$p\""]) })
            out.push({ icon: "search", text: I18n.tr("Mostrar en su carpeta"),
                run: () => Quickshell.execDetached(["bash", "-c", expand + "d=\"$p\"; [ -d \"$d\" ] || d=$(dirname \"$d\"); [ -d \"$d\" ] && xdg-open \"$d\""]) })
        } else if (color)
            out.push({ icon: "palette", swatch: color, text: I18n.tr("Copiar color %1").arg(color.toUpperCase()), run: () => copy(color.toUpperCase()) })
        else if (sum)
            out.push({ icon: "calculator", text: ellipsis(t, 22) + " = " + sum, hint: I18n.tr("Copiar"), run: () => copy(sum) })
        return out
    }

    // ── menu contents ───────────────────────────────────────────────────
    function appEntry(w) { return w && w.app_id ? DesktopEntries.heuristicLookup(w.app_id) : null }

    function withWindow(w, fn) {
        // Window actions act on the focused window; the click focused this
        // one, but something may have taken focus since.
        if (!Compositor.focused || Compositor.focused.id !== w.id) Compositor.focusWindow(w.id)
        fn()
    }

    function windowItems(w) {
        const out = []
        const act = name => () => withWindow(w, () => Compositor.action(name))
        const filled = w.mode && w.mode !== "Normal"
        out.push({ icon: "scan", text: I18n.tr("Centrar en pantalla"), run: act("center-window") })
        out.push({ icon: "maximize", text: filled ? I18n.tr("Restaurar tamaño") : I18n.tr("Ajustar a la pantalla"), run: act("fit-window") })
        out.push({ icon: "monitor", text: I18n.tr("Pantalla completa"), run: act("toggle-fullscreen") })
        out.push({ icon: "pin", text: I18n.tr("Fijar a la pantalla"), run: () => Compositor.request({ Pin: { window: w.id, value: true } }) })
        out.push({ icon: "image", text: I18n.tr("Capturar ventana"), hint: I18n.tr("al portapapeles"),
            run: () => withWindow(w, () => Quickshell.execDetached(["sh", "-c", "sleep 0.25; driftwm msg screenshot window -o - | wl-copy"])) })
        out.push({ separator: true })
        out.push({ icon: "moon", text: I18n.tr("Suspender"), run: () => Compositor.request({ Suspend: w.id }) })
        out.push({ id: "close", icon: "x", text: I18n.tr("Cerrar ventana"), danger: true, run: () => Compositor.closeWindow(w.id) })
        return out
    }

    function appItems(w) {
        const out = []
        const app = appEntry(w)
        if (root.terminals.includes(w.app_id))
            out.push({ icon: "terminal", text: I18n.tr("Nueva terminal"), run: () => Session.terminal() })
        else if (app)
            out.push({ icon: "plus", text: I18n.tr("Nueva ventana de %1").arg(ellipsis(app.name, 22)), run: () => app.execute() })
        if (app) {
            for (const a of (app.actions || []).slice(0, 5))
                out.push({ icon: "sparkles", text: ellipsis(a.name, 34), run: () => a.execute() })
        }
        return out
    }

    // The desktop icons are an optional part of the shell, reached over its
    // own IPC so this menu works with or without them.
    function desktopCall(fn) { Quickshell.execDetached(["qs", "-p", Quickshell.shellDir, "ipc", "call", "desktop", fn]) }

    function newDocument() {
        // Same folder the icons list; noclobber so a name is never reused.
        Quickshell.execDetached(["sh", "-c", `d="\${WONDERLAND_DESKTOP_DIR:-$(xdg-user-dir DESKTOP 2>/dev/null)}"
            [ -n "$d" ] && [ "$d" != "$HOME" ] || d="$HOME/Desktop"
            mkdir -p "$d" || exit 1
            set -C; n="Documento sin título"; f="$d/$n.txt"; i=2
            until { true > "$f"; } 2>/dev/null; do [ $i -gt 999 ] && exit 1; f="$d/$n $i.txt"; i=$((i+1)); done`])
    }

    function desktopItems() {
        if (!root.desktop) return []
        return [
            { header: I18n.tr("Escritorio") },
            { icon: "plus", text: I18n.tr("Nueva carpeta"), run: () => desktopCall("newFolder") },
            { icon: "plus", text: I18n.tr("Nuevo documento de texto"), run: () => newDocument() },
            { icon: "layout-grid", text: I18n.tr("Ordenar iconos"), run: () => desktopCall("cleanUp") },
            { icon: "hard-drive", text: I18n.tr("Abrir la carpeta Escritorio"), run: () => desktopCall("openFolder") },
        ]
    }

    function canvasItems() {
        const out = []
        const wins = Compositor.windows
        out.push({ icon: "terminal", text: I18n.tr("Nueva terminal"), run: () => Session.terminal() })
        out.push({ icon: "search", text: I18n.tr("Buscar o abrir…"), hint: I18n.tr("Super+Espacio"), run: () => Popups.spotlightRequested() })
        out.push({ separator: true })
        if (wins.length > 1)
            out.push({ icon: "zoom-out", text: I18n.tr("Ver todas las ventanas"), hint: wins.length + "", run: () => Compositor.action("zoom-to-fit") })
        if (Math.abs(Compositor.zoom - 1) > 0.01)
            out.push({ icon: "rotate-ccw", text: I18n.tr("Zoom al 100 %"), hint: Math.round(Compositor.zoom * 100) + " %", run: () => Compositor.action("zoom-reset") })
        out.push({ icon: "house", text: I18n.tr("Ir al inicio"), run: () => Compositor.action("home-toggle") })
        if (Quickshell.screens.length > 1)
            out.push({ icon: "layout-grid", text: I18n.tr("Alinear pantallas"), run: () => Compositor.action("align-outputs") })

        // The windows nearest in use, to jump to without hunting the canvas.
        const others = wins.filter(w => !w.is_focused).slice(0, 5)
        if (others.length) {
            out.push({ header: I18n.tr("Ir a") })
            for (const w of others) {
                const app = appEntry(w)
                out.push({ icon: w.suspended ? "moon" : "app-window", iconSource: app ? Quickshell.iconPath(app.icon, true) : "",
                    text: ellipsis(w.title || w.app_id, 34), run: () => w.suspended ? Compositor.relaunch(w.id) : Compositor.focusWindow(w.id) })
            }
        }
        out.push({ separator: true })
        out.push({ icon: "image", text: I18n.tr("Fondos de pantalla…"), run: () => Popups.wallpapersRequested() })
        out.push({ icon: "monitor", text: I18n.tr("Pantallas…"), run: () => Popups.displaysRequested() })
        out.push({ icon: Theme.dark ? "sun" : "moon", text: Theme.dark ? I18n.tr("Tema claro") : I18n.tr("Tema oscuro"),
            run: () => Settings.data.themeMode = Theme.dark ? "light" : "dark" })
        out.push({ icon: "settings", text: I18n.tr("Sistema…"),
            run: () => Quickshell.execDetached(["sh", "-c", "command -v wonderland-sistema >/dev/null && exec wonderland-sistema; exec systemsettings"]) })
        return out
    }

    function section(out, items) {
        if (!items.length) return
        if (out.length && !out[out.length - 1].separator && !items[0].header) out.push({ separator: true })
        for (const i of items) out.push(i)
    }

    function rebuild() {
        const out = []
        const w = root.win
        if (root.target === "canvas") {
            section(out, clipboardItems(root.clipboard))
            section(out, desktopItems())
            section(out, canvasItems())
        } else {
            const app = appEntry(w)
            out.push({ title: ellipsis(app ? app.name : w.app_id || I18n.tr("Ventana"), 30), detail: ellipsis(w.title, 44),
                iconSource: app ? Quickshell.iconPath(app.icon, true) : "" })
            section(out, appItems(w))
            section(out, windowItems(w))
        }
        out.forEach((item, i) => { if (!item.id) item.id = "i" + i })
        root.items = out
    }

    Process {
        id: desktopProbe
        command: ["qs", "-p", Quickshell.shellDir, "ipc", "show"]
        stdout: StdioCollector {
            onStreamFinished: {
                const shown = /^target desktop$/m.test(text) && Settings.data.desktopIcons !== false
                if (shown === root.desktop) return
                root.desktop = shown
                if (root.open && root.target === "canvas") root.rebuild()
            }
        }
    }

    Process {
        id: clip
        command: ["sh", "-c", `timeout 0.4 wl-paste --no-newline --type text 2>/dev/null | head -c ${root.maxText}`]
        stdout: StdioCollector {
            onStreamFinished: {
                // Only worth a row when it is something to open.
                if (!root.open || !root.clipboardItems(text).length) return
                root.clipboard = text
                root.rebuild()
            }
        }
    }

    PanelWindow {
        id: layer
        screen: root.screen
        visible: root.open
        WlrLayershell.namespace: "wonderland-context-menu"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
        exclusionMode: ExclusionMode.Ignore
        anchors { top: true; bottom: true; left: true; right: true }
        color: "transparent"

        onVisibleChanged: if (visible) { keys.forceActiveFocus(); intro.restart() }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: root.close()
        }

        Item {
            id: keys
            focus: true
            Keys.onEscapePressed: root.close()
        }

        Rectangle {
            id: card
            readonly property int edge: 8
            x: Math.max(edge, Math.min(root.clickX, layer.width - width - edge))
            // Flip above the pointer rather than squeeze against the bottom.
            y: root.clickY + height + edge > layer.height
                ? Math.max(edge, root.clickY - height)
                : root.clickY
            width: Math.max(220, column.implicitWidth + 10)
            height: column.implicitHeight + 10
            radius: Theme.radius
            readonly property color base: Theme.p.elevated
            color: Qt.rgba(base.r, base.g, base.b, Theme.dark ? 0.94 : 0.96)
            border.width: 1
            border.color: Theme.p.stroke2
            transformOrigin: Item.TopLeft

            ParallelAnimation {
                id: intro
                NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: Theme.animMs * 0.6 }
                NumberAnimation { target: card; property: "scale"; from: 0.96; to: 1; duration: Theme.animMs * 0.6; easing.type: Easing.OutCubic }
            }

            // Swallow clicks on the card's padding so they don't close it.
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

            ColumnLayout {
                id: column
                x: 5
                y: 5
                width: card.width - 10
                spacing: 0

                Repeater {
                    model: root.items

                    delegate: Loader {
                        id: slot
                        required property var modelData
                        Layout.fillWidth: true
                        sourceComponent: modelData.separator ? separator
                            : modelData.header ? header
                            : modelData.title ? title : row

                        Component {
                            id: separator
                            Item {
                                implicitHeight: 9
                                Rectangle {
                                    anchors { left: parent.left; right: parent.right; margins: 8; verticalCenter: parent.verticalCenter }
                                    height: 1
                                    color: Theme.p.stroke2
                                }
                            }
                        }

                        Component {
                            id: header
                            Item {
                                implicitHeight: 24
                                implicitWidth: headerText.implicitWidth + 20
                                Text {
                                    id: headerText
                                    anchors { left: parent.left; leftMargin: 10; bottom: parent.bottom; bottomMargin: 4 }
                                    text: slot.modelData.header
                                    color: Theme.p.text3
                                    font.pixelSize: Theme.smallFontSize
                                    font.weight: Font.DemiBold
                                    font.family: Theme.fontFamily
                                }
                            }
                        }

                        Component {
                            id: title
                            Item {
                                implicitHeight: 42
                                implicitWidth: 34 + Math.max(titleText.implicitWidth, detailText.implicitWidth) + 20
                                Image {
                                    id: titleIcon
                                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                                    width: 24; height: 24
                                    sourceSize: Qt.size(48, 48)
                                    source: slot.modelData.iconSource || ""
                                    visible: status === Image.Ready
                                }
                                Icon {
                                    anchors.centerIn: titleIcon
                                    visible: !titleIcon.visible
                                    name: "app-window"
                                    size: 18
                                    color: Theme.p.text2
                                }
                                Column {
                                    anchors { left: titleIcon.right; leftMargin: 9; verticalCenter: parent.verticalCenter }
                                    Text {
                                        id: titleText
                                        text: slot.modelData.title
                                        color: Theme.p.text
                                        font.pixelSize: Theme.fontSize
                                        font.weight: Font.DemiBold
                                        font.family: Theme.fontFamily
                                    }
                                    Text {
                                        id: detailText
                                        text: slot.modelData.detail || ""
                                        visible: text !== ""
                                        color: Theme.p.text3
                                        font.pixelSize: Theme.smallFontSize
                                        font.family: Theme.fontFamily
                                    }
                                }
                            }
                        }

                        Component {
                            id: row
                            Item {
                                id: entry
                                readonly property var item: slot.modelData
                                readonly property bool armed: root.armed === item.id
                                readonly property bool lit: mouse.containsMouse || armed
                                readonly property color ink: lit ? Theme.p.onAccent : item.danger ? Theme.p.red : Theme.p.text
                                implicitHeight: 28
                                implicitWidth: 36 + label.implicitWidth + (hint.visible ? hint.implicitWidth + 24 : 0) + 12

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 6
                                    color: entry.armed || (entry.item.danger && mouse.containsMouse) ? Theme.p.red : Theme.p.accent
                                    visible: entry.lit
                                }
                                Image {
                                    id: appIcon
                                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                                    width: 16; height: 16
                                    sourceSize: Qt.size(32, 32)
                                    source: entry.item.iconSource || ""
                                    visible: status === Image.Ready
                                }
                                Rectangle {
                                    anchors.centerIn: appIcon
                                    visible: !!entry.item.swatch
                                    width: 14; height: 14; radius: 4
                                    color: entry.item.swatch || "transparent"
                                    border.width: 1
                                    border.color: Theme.p.stroke1
                                }
                                Icon {
                                    anchors.centerIn: appIcon
                                    visible: !appIcon.visible && !entry.item.swatch
                                    name: entry.item.icon || ""
                                    size: 15
                                    color: entry.lit ? Theme.p.onAccent : entry.item.danger ? Theme.p.red : Theme.p.text2
                                }
                                Text {
                                    id: label
                                    anchors { left: parent.left; leftMargin: 36; verticalCenter: parent.verticalCenter }
                                    text: entry.armed ? I18n.tr("Pulsa otra vez para confirmar") : entry.item.text
                                    color: entry.ink
                                    font.pixelSize: Theme.fontSize
                                    font.family: Theme.fontFamily
                                }
                                Text {
                                    id: hint
                                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                    text: entry.item.hint || ""
                                    visible: text !== "" && !entry.armed
                                    color: entry.lit ? Theme.p.onAccent : Theme.p.text3
                                    opacity: entry.lit ? 0.85 : 1
                                    font.pixelSize: Theme.smallFontSize
                                    font.family: Theme.fontFamily
                                }
                                MouseArea {
                                    id: mouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: root.run(entry.item)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    IpcHandler {
        target: "contextmenu"
        function open(target: string, x: int, y: int, output: string, window: string): void {
            root.show(target, x, y, output, window)
        }
        function close(): void { root.close() }
    }
}
