import QtQuick
import QtQuick.Controls.Basic

/// The copies that saving has left behind: look at one, and bring it back.
Popup {
    id: panel
    property var entries: []
    property int at: 0
    property int page: 0
    readonly property var entry: entries[at] || null
    function show() {
        entries = App.versions()
        at = 0
        page = 0
        open()
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, parent.width - 60)
    height: Math.min(470, parent.height - 60)
    padding: 0
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: "#40000000" }
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    contentItem: Item {
        Text {
            x: 18
            y: 16
            text: "Versiones de «" + App.deck.name + "»"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        TButton {
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 11
            symbol: "x"
            onClicked: panel.close()
        }
        Column {
            visible: panel.entries.length === 0
            anchors.centerIn: parent
            width: 340
            spacing: 8
            Sym { anchors.horizontalCenter: parent.horizontalCenter; name: "clock"; size: 28; color: Theme.faint }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: App.deck.path === "" ? "Guarda la presentación: cada vez que lo hagas quedará aquí una versión a la que volver."
                    : "Todavía no hay versiones. Cada guardado deja una; se conservan las veinte últimas."
                color: Theme.dim
                font.pixelSize: 13
            }
        }
        ListView {
            id: list
            visible: panel.entries.length > 0
            x: 10
            y: 50
            width: 230
            height: parent.height - 60
            clip: true
            model: panel.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { width: 7 }
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: list.width - 8
                height: 44
                radius: 7
                color: panel.at === index ? Theme.accent : (rowArea.containsMouse ? Theme.hover : "transparent")
                Column {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        text: row.modelData.date + ", " + row.modelData.time
                        color: panel.at === row.index ? "#ffffff" : Theme.text
                        font.pixelSize: 13
                    }
                    Text {
                        text: (row.index === 0 ? "La más reciente · " : "") + row.modelData.size
                        color: panel.at === row.index ? "#ccffffff" : Theme.dim
                        font.pixelSize: 11
                    }
                }
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { panel.at = row.index; panel.page = 0 }
                }
            }
        }
        Rectangle {
            visible: panel.entries.length > 0
            x: 248
            y: 50
            width: 1
            height: parent.height - 60
            color: Theme.line
        }
        Item {
            visible: panel.entry !== null
            x: 262
            y: 50
            width: parent.width - 280
            height: parent.height - 64
            Rectangle {
                id: shot
                width: parent.width
                height: parent.height - 44
                radius: 6
                color: Theme.desk
                Image {
                    id: picture
                    anchors.fill: parent
                    anchors.margins: 12
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 900
                    source: panel.entry ? App.previewOf(panel.entry.file, panel.page) : ""
                }
            }
            Row {
                anchors.bottom: parent.bottom
                spacing: 2
                TButton { symbol: "chevron-left"; active: panel.page > 0; onClicked: panel.page -= 1 }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Diapositiva " + (panel.page + 1)
                    color: Theme.dim
                    font.pixelSize: 12
                }
                TButton { symbol: "chevron-right"; onClicked: panel.page += 1 }
            }
            DialogButton {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                text: "Restaurar esta versión"
                primary: true
                onClicked: {
                    const file = panel.entry.file
                    panel.close()
                    App.restoreVersion(file)
                }
            }
        }
    }
}
