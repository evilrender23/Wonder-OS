import QtQuick
import QtQuick.Layouts
import Quickshell
import ".."
import "../services"

// WonderlandWM's own help entries. They are the bar's Ayuda menu, and close
// the app's Ayuda menu when the focused app brings one.
ColumnLayout {
    id: root
    signal done()

    Layout.fillWidth: true
    spacing: 0

    MenuRow { text: "Buscar…"; shortcut: "❖ Espacio"; onTriggered: { root.done(); Popups.spotlightRequested() } }
    MenuSeparator {}
    MenuRow {
        text: "Ayuda de WonderlandWM"
        onTriggered: { root.done(); Quickshell.execDetached(["xdg-open", "https://github.com/evilrender23/wonderlandwm#readme"]) }
    }
    MenuRow {
        text: "Atajos de teclado"
        onTriggered: { root.done(); Quickshell.execDetached(["xdg-open", "https://github.com/evilrender23/wonderlandwm/blob/main/config.reference.toml"]) }
    }
}
