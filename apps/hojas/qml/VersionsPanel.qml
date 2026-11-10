import QtQuick
import QtQuick.Controls.Basic

/// The copies each save left behind: look at one, and bring it back.
Popup {
    id: panel
    required property var host
    readonly property var book: host.book
    property var list: []
    property int current: -1
    property var preview: []

    function show() {
        list = book.versions()
        current = list.length > 0 ? 0 : -1
        open()
    }
    onCurrentChanged: preview = current >= 0 && current < list.length ? book.versionPreview(list[current].file) : []

    modal: true
    anchors.centerIn: parent
    width: Math.min(700, parent.width - 60)
    height: Math.min(460, parent.height - 60)
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 12; color: Theme.menu; border.color: Theme.border }
    Overlay.modal: Rectangle { color: "#40000000" }
    contentItem: Item {
        Text {
            id: heading
            text: "Versiones de «" + panel.book.title + "»"
            width: parent.width
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            visible: panel.list.length === 0
            y: 34
            width: parent.width
            wrapMode: Text.Wrap
            text: panel.book.path === "" ? "Este documento aún no se ha guardado. Cada vez que lo guardes quedará aquí una copia, hasta veinte."
                : "Todavía no hay versiones. Cada vez que guardes quedará aquí una copia, hasta veinte."
            color: Theme.dim
            font.pixelSize: 13
        }
        ListView {
            id: versions
            visible: panel.list.length > 0
            y: 34
            width: 216
            height: parent.height - 34 - 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.list
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                width: versions.width
                height: 42
                radius: 7
                color: panel.current === index ? Theme.accent : (entryArea.containsMouse ? Theme.hover : "transparent")
                Column {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    Text { text: entry.modelData.when; color: panel.current === entry.index ? "#ffffff" : Theme.text; font.pixelSize: 13 }
                    Text { text: entry.modelData.size; color: panel.current === entry.index ? "#d0ffffff" : Theme.dim; font.pixelSize: 11 }
                }
                MouseArea { id: entryArea; anchors.fill: parent; hoverEnabled: true; onClicked: panel.current = entry.index }
            }
        }
        Rectangle {
            visible: panel.list.length > 0
            x: 228
            y: 34
            width: parent.width - 228
            height: parent.height - 34 - 40
            radius: 8
            color: "#ffffff"
            border.color: Theme.border
            clip: true
            MiniSheet {
                anchors.fill: parent
                anchors.margins: 1
                radius: 7
                rows: panel.preview
                rowHeight: 21
                fontSize: 11
            }
        }
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 8
            DialogButton { text: "Cerrar"; onClicked: panel.close() }
            DialogButton {
                text: "Restaurar esta versión"
                primary: true
                active: panel.current >= 0
                onClicked: {
                    const file = panel.list[panel.current].file
                    panel.close()
                    if (panel.host.done(panel.book.restoreVersion(file)))
                        panel.host.say("Versión restaurada. Deshacer la devuelve a como estaba.")
                }
            }
        }
    }
}
