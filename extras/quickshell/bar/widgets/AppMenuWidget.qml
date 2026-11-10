import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// The macOS app menus: the focused app's name in bold, the app's own menu bar
// when it exports one (Archivo, Editar…: services/AppMenu.qml), then Ventana,
// Lienzo and Ayuda. Window actions go to the compositor (they act on the
// focused window: the bar takes no keyboard focus, so it stays focused while a
// menu is open). Hovering a title while one of these menus is open switches
// menus.
Item {
    id: root

    property var bar
    property string widgetId: "appmenu"

    // The window the menus act on: the focused one, or the last one focused
    // (a popup grab can blank the focus flag for a moment).
    readonly property var target: AppMenu.target
    readonly property bool hasWindow: AppMenu.hasWindow
    readonly property var entry: hasWindow && target ? DesktopEntries.heuristicLookup(target.app_id) : null
    readonly property string appName: !hasWindow || !target ? "Escritorio"
        : entry && entry.name ? entry.name : prettify(target.app_id)
    property string current: ""          // id of the open menu, "" when none

    implicitWidth: row.implicitWidth
    implicitHeight: Theme.pillHeight

    function prettify(id) {
        const last = (id || "").split(".").pop()
        return last ? last.charAt(0).toUpperCase() + last.slice(1) : "App"
    }

    readonly property var menus: ({ app: appPanel, window: windowPanel, canvas: canvasPanel, help: helpPanel })

    function toggle(id) { menus[id].visible = !menus[id].visible }
    function hoverTo(id) { if (current !== "" && current !== id) menus[id].visible = true }
    function act(cmd) { Compositor.action(cmd) }
    function closeAll() { if (Popups.current) Popups.current.visible = false }

    // The app's own menus go between its name and Ventana. Its Ayuda stays
    // last, as on macOS: it takes the place of the bar's own Ayuda and carries
    // WonderlandWM's help entries too.
    function isHelpLabel(label) { return ["ayuda", "help"].includes((label || "").trim().toLowerCase()) }
    readonly property var appItems: AppMenu.items.filter(i => !isHelpLabel(i.label))
    readonly property var appHelp: AppMenu.items.filter(i => isHelpLabel(i.label)).slice(0, 1)
    readonly property bool appHasHelp: appHelp.length > 0

    // `qs ipc call appmenu menu <n>` opens the app's n-th menu.
    Connections {
        target: AppMenu
        function onShowRequested(index) {
            if (!root.QsWindow.window || root.QsWindow.window.screen !== Quickshell.screens[0]) return
            const wanted = AppMenu.items[index]
            if (!wanted) return
            for (const titles of [appTitles, appHelpTitle]) {
                for (let i = 0; i < titles.count; i++) {
                    const title = titles.itemAt(i)
                    if (title && title.entry.id === wanted.id) { title.show(true); return }
                }
            }
        }
    }

    // `qs ipc call bar togglePanel appmenu` opens the app's menu (as macOS'
    // Ctrl+F2 focuses the menu bar).
    Connections {
        target: Popups
        function onPanelRequested(id) {
            if (id === root.widgetId && root.QsWindow.window && root.QsWindow.window.screen === Quickshell.screens[0]) {
                if (root.current !== "") root.closeAll()
                else { appPanel.fromIpc = true; appPanel.visible = true }
            }
        }
    }

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        MenuTitle { id: appTitle; text: root.appName; bold: true; open: root.current === "app"; onPressed: root.toggle("app"); onEntered: root.hoverTo("app") }
        Repeater {
            id: appTitles
            model: root.appItems
            delegate: AppMenuTitle {
                required property var modelData
                owner: root
                entry: modelData
            }
        }
        MenuTitle { id: windowTitle; text: "Ventana"; open: root.current === "window"; onPressed: root.toggle("window"); onEntered: root.hoverTo("window") }
        MenuTitle { id: canvasTitle; text: "Lienzo"; open: root.current === "canvas"; onPressed: root.toggle("canvas"); onEntered: root.hoverTo("canvas") }
        MenuTitle { id: helpTitle; visible: !root.appHasHelp; text: "Ayuda"; open: root.current === "help"; onPressed: root.toggle("help"); onEntered: root.hoverTo("help") }
        Repeater {
            id: appHelpTitle
            model: root.appHelp
            delegate: AppMenuTitle {
                required property var modelData
                owner: root
                entry: modelData
            }
        }
    }

    // ── App ─────────────────────────────────────────────────────────────
    MenuPanel {
        id: appPanel
        target: appTitle
        onVisibleChanged: root.current = visible ? "app" : (root.current === "app" ? "" : root.current)

        MenuRow {
            text: `Acerca de ${root.appName}`
            enabled: root.entry !== null
            onTriggered: { appPanel.close(); Quickshell.execDetached(["sh", "-c", `xdg-open "https://www.google.com/search?q=${encodeURIComponent(root.appName)}"`]) }
        }
        MenuSeparator {}
        MenuRow {
            text: "Nueva ventana"
            enabled: root.entry !== null
            onTriggered: { appPanel.close(); root.entry.execute() }
        }
        MenuSeparator {}
        MenuRow {
            text: `Suspender ${root.appName}`
            enabled: root.hasWindow
            onTriggered: { appPanel.close(); Compositor.suspendWindow(root.target.id) }
        }
        MenuRow {
            text: `Cerrar ${root.appName}`
            shortcut: "❖ Q"
            enabled: root.hasWindow
            onTriggered: { appPanel.close(); Compositor.closeWindow(root.target.id) }
        }
    }

    // ── Ventana ─────────────────────────────────────────────────────────
    MenuPanel {
        id: windowPanel
        target: windowTitle
        contentWidth: 270
        onVisibleChanged: root.current = visible ? "window" : (root.current === "window" ? "" : root.current)

        MenuRow { text: "Ajustar a la rejilla"; shortcut: "❖ M"; enabled: root.hasWindow; onTriggered: { windowPanel.close(); root.act("fit-window-snapped") } }
        MenuRow { text: "Ajustar a la pantalla"; shortcut: "⇧❖ M"; enabled: root.hasWindow; onTriggered: { windowPanel.close(); root.act("fit-window") } }
        MenuRow { text: "Rellenar"; enabled: root.hasWindow; onTriggered: { windowPanel.close(); root.act("fill-window") } }
        MenuRow { text: "Centrar"; shortcut: "❖ C"; enabled: root.hasWindow; onTriggered: { windowPanel.close(); root.act("center-window") } }
        MenuSeparator {}
        MenuRow { text: "Pantalla completa"; shortcut: "❖ F"; enabled: root.hasWindow; onTriggered: { windowPanel.close(); root.act("toggle-fullscreen") } }
        MenuRow { text: "Fijar a la pantalla"; shortcut: "❖ T"; enabled: root.hasWindow; onTriggered: { windowPanel.close(); root.act("toggle-pin-to-screen") } }
        MenuSeparator { visible: Compositor.windows.length > 0 }
        Repeater {
            model: Compositor.windows
            delegate: MenuRow {
                required property var modelData
                readonly property bool isTarget: root.target !== null && modelData.id === root.target.id
                text: `${isTarget ? "✓  " : "     "}${modelData.title || modelData.app_id}`
                onTriggered: {
                    windowPanel.close()
                    if (modelData.suspended) Compositor.relaunch(modelData.id)
                    else Compositor.focusWindow(modelData.id)
                }
            }
        }
    }

    // ── Lienzo ──────────────────────────────────────────────────────────
    MenuPanel {
        id: canvasPanel
        target: canvasTitle
        onVisibleChanged: root.current = visible ? "canvas" : (root.current === "canvas" ? "" : root.current)

        MenuRow { text: "Acercar"; shortcut: "❖ +"; onTriggered: root.act("zoom-in") }
        MenuRow { text: "Alejar"; shortcut: "❖ −"; onTriggered: root.act("zoom-out") }
        MenuRow { text: "Tamaño real"; shortcut: "❖ 0"; onTriggered: { canvasPanel.close(); root.act("zoom-reset") } }
        MenuSeparator {}
        MenuRow { text: "Vista general"; shortcut: "❖ W"; onTriggered: { canvasPanel.close(); root.act("zoom-to-fit") } }
        MenuRow { text: "Ir al inicio"; shortcut: "❖ A"; onTriggered: { canvasPanel.close(); root.act("home-toggle") } }
        MenuSeparator { visible: Compositor.bookmarkNames.length > 0 }
        Repeater {
            model: Compositor.bookmarkNames
            delegate: MenuRow {
                required property string modelData
                text: `${modelData === Compositor.activeBookmark ? "✓  " : "     "}${modelData}`
                onTriggered: { canvasPanel.close(); Compositor.goToBookmark(modelData) }
            }
        }
    }

    // ── Ayuda ───────────────────────────────────────────────────────────
    MenuPanel {
        id: helpPanel
        target: helpTitle
        onVisibleChanged: root.current = visible ? "help" : (root.current === "help" ? "" : root.current)

        ShellHelpRows { onDone: helpPanel.close() }
    }
}
