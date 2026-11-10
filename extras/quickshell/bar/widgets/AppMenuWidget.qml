import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// The macOS app menus: the focused app's name in bold, then the app's own
// menu bar when it exports one (Archivo, Editar… Ayuda: services/AppMenu.qml).
// Hovering a title while one of these menus is open switches menus.
Item {
    id: root

    property var bar
    property string widgetId: "appmenu"

    // The window the menus act on: the focused one, or the last one focused
    // (a popup grab can blank the focus flag for a moment).
    readonly property var target: AppMenu.target
    readonly property bool hasWindow: AppMenu.hasWindow
    readonly property var entry: hasWindow && target ? DesktopEntries.heuristicLookup(target.app_id) : null
    readonly property string appName: !hasWindow || !target ? I18n.tr("Escritorio")
        : entry && entry.name ? entry.name : prettify(target.app_id)
    property string current: ""          // id of the open menu, "" when none

    implicitWidth: row.implicitWidth
    implicitHeight: Theme.pillHeight

    function prettify(id) {
        const last = (id || "").split(".").pop()
        return last ? last.charAt(0).toUpperCase() + last.slice(1) : "App"
    }

    readonly property var menus: ({ app: appPanel })

    function toggle(id) { menus[id].visible = !menus[id].visible }
    function hoverTo(id) { if (current !== "" && current !== id) menus[id].visible = true }
    function closeAll() { if (Popups.current) Popups.current.visible = false }

    // `qs ipc call appmenu menu <n>` opens the app's n-th menu.
    Connections {
        target: AppMenu
        function onShowRequested(index) {
            if (!root.QsWindow.window || root.QsWindow.window.screen !== Quickshell.screens[0]) return
            const wanted = AppMenu.items[index]
            if (!wanted) return
            for (let i = 0; i < appTitles.count; i++) {
                const title = appTitles.itemAt(i)
                if (title && title.entry.id === wanted.id) { title.show(true); return }
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
            model: AppMenu.items
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
            text: I18n.tr("Acerca de %1").arg(root.appName)
            enabled: root.entry !== null
            onTriggered: { appPanel.close(); Quickshell.execDetached(["sh", "-c", `xdg-open "https://www.google.com/search?q=${encodeURIComponent(root.appName)}"`]) }
        }
        MenuSeparator {}
        MenuRow {
            text: I18n.tr("Nueva ventana")
            enabled: root.entry !== null
            onTriggered: { appPanel.close(); root.entry.execute() }
        }
        MenuSeparator {}
        MenuRow {
            text: I18n.tr("Suspender %1").arg(root.appName)
            enabled: root.hasWindow
            onTriggered: { appPanel.close(); Compositor.suspendWindow(root.target.id) }
        }
        MenuRow {
            text: I18n.tr("Cerrar %1").arg(root.appName)
            shortcut: "❖ Q"
            enabled: root.hasWindow
            onTriggered: { appPanel.close(); Compositor.closeWindow(root.target.id) }
        }
    }
}
