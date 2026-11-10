import QtQuick
import QtQuick.Controls.Basic

/// The copies each save by hand left behind: look at one, bring it back.
Popup {
    id: panel
    property int current: 0
    readonly property var list: App.versions

    onOpened: current = 0
    modal: true
    anchors.centerIn: parent
    width: Math.min(680, parent.width - 60)
    height: Math.min(440, parent.height - 60)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: "#40000000" }
    contentItem: Item {
        Text {
            x: 18
            y: 16
            text: "Versiones"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            x: 18
            y: 38
            text: "Cada vez que guardas queda una copia; se conservan las 20 últimas."
            color: Theme.dim
            font.pixelSize: 12
        }
        Text {
            visible: panel.list.length === 0
            anchors.centerIn: parent
            text: "Todavía no hay versiones: guarda el documento con Ctrl S."
            color: Theme.dim
            font.pixelSize: 13
        }
        ListView {
            id: view
            visible: panel.list.length > 0
            x: 10
            y: 68
            width: 210
            height: parent.height - y - 58
            clip: true
            model: panel.list
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                width: view.width
                height: 32
                radius: 7
                color: panel.current === index ? Theme.accent : (entryHit.containsMouse ? Theme.hover : "transparent")
                Sym {
                    x: 9
                    anchors.verticalCenter: parent.verticalCenter
                    name: "clock"
                    size: 14
                    color: panel.current === entry.index ? "#ffffff" : Theme.dim
                }
                Text {
                    x: 32
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.modelData.label
                    color: panel.current === entry.index ? "#ffffff" : Theme.text
                    font.pixelSize: 13
                }
                MouseArea {
                    id: entryHit
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.current = entry.index
                }
            }
        }
        Rectangle {
            visible: panel.list.length > 0
            x: 230
            y: 68
            width: parent.width - x - 18
            height: parent.height - y - 58
            radius: 9
            color: Theme.canvas
            border.color: Theme.border
            Image {
                anchors.fill: parent
                anchors.margins: 10
                fillMode: Image.PreserveAspectFit
                sourceSize: Qt.size(900, 600)
                source: panel.visible && panel.current < panel.list.length ? "image://formula/file/" + String(Theme.text).slice(1) + "/" + panel.list[panel.current].path : ""
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 15
            spacing: 8
            DialogButton {
                text: "Cerrar"
                onClicked: panel.close()
            }
            DialogButton {
                text: "Restaurar"
                primary: true
                active: panel.current < panel.list.length
                onClicked: {
                    const path = panel.list[panel.current].path
                    panel.close()
                    App.restoreVersion(path)
                }
            }
        }
    }
}
