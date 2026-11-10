import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import ".."
import "../services"
import "../components"

// The menu of the selected desktop icons. It is drawn inside the desktop
// surface, not as a popup: Desktop widens its input region while it is open.
Item {
    id: menu

    required property var desk
    readonly property var names: desk.selectedNames
    readonly property bool single: names.length === 1
    readonly property bool folder: single && desk.items.some(it => it.name === names[0] && it.kind === "dir")

    function pick(action) {
        desk.menuOpen = false
        action()
    }

    width: Math.max(210, rows.implicitWidth + 12)
    height: rows.implicitHeight + 12

    Rectangle {
        id: plate
        anchors.fill: parent
        radius: Theme.popupRadius
        color: Theme.p.elevated
        border.width: 1
        border.color: Theme.p.stroke2
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.p.shadow
            shadowOpacity: 0.3
            shadowBlur: 0.8
            shadowVerticalOffset: 4
        }
    }

    // Presses on the padding between rows stay in the menu.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    ColumnLayout {
        id: rows
        anchors { fill: parent; margins: 6 }
        spacing: 0

        MenuRow {
            text: menu.single ? "Abrir" : `Abrir ${menu.names.length} elementos`
            shortcut: "↵"
            onTriggered: menu.pick(() => menu.desk.open(menu.names))
        }
        MenuRow {
            visible: menu.single
            text: "Mostrar en el gestor de archivos"
            onTriggered: menu.pick(() => menu.desk.run(["reveal", menu.names[0]]))
        }
        MenuRow {
            visible: menu.single
            text: "Renombrar"
            shortcut: "F2"
            onTriggered: menu.pick(() => menu.desk.renaming = menu.names[0])
        }
        MenuRow {
            visible: menu.folder
            text: "Personalizar carpeta…"
            onTriggered: menu.pick(() => menu.desk.run(["customize", menu.names[0]]))
        }
        MenuRow {
            text: "Mover a la papelera"
            shortcut: "Supr"
            onTriggered: menu.pick(() => menu.desk.trash(menu.names))
        }
        MenuSeparator {}
        MenuRow {
            text: "Nueva carpeta"
            onTriggered: menu.pick(() => menu.desk.newFolder(true))
        }
        MenuRow {
            text: "Ordenar iconos"
            onTriggered: menu.pick(() => menu.desk.cleanUp())
        }
        MenuRow {
            text: Settings.data.desktopMagnet ? "Desactivar el imán" : "Activar el imán"
            onTriggered: menu.pick(() => Settings.data.desktopMagnet = !Settings.data.desktopMagnet)
        }
    }
}
