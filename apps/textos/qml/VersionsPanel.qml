import QtQuick
import QtQuick.Controls.Basic

/// The copies every save leaves behind: look at one, and go back to it.
Popup {
    id: panel
    property string picked
    property string preview
    property var facts: ({})
    signal restore(string file)

    function pick(file) {
        picked = file
        preview = file === "" ? "" : App.versionPreview(file)
        facts = file === "" ? ({}) : App.versionFacts(file)
    }

    modal: true
    anchors.centerIn: parent
    width: Math.min(700, parent.width - 60)
    height: Math.min(480, parent.height - 60)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: pick(App.versions.length > 0 ? App.versions[0].file : "")
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: "#40000000"
    }
    contentItem: Item {
        Text {
            id: heading
            x: 18
            y: 16
            text: "Versiones de «" + App.title + "»"
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
        Text {
            visible: list.count === 0
            anchors.centerIn: parent
            width: 340
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            lineHeight: 1.25
            text: App.editor.path === "" ? "Este documento todavía no se ha guardado. Cada vez que lo guardes (Ctrl+S) quedará aquí una copia a la que volver."
                                         : "Cada vez que guardes (Ctrl+S) quedará aquí una copia a la que volver. Se conservan las veinte últimas."
            color: Theme.dim
            font.pixelSize: 13
        }
        ListView {
            id: list
            x: 10
            y: 50
            width: 214
            height: parent.height - y - 12
            visible: count > 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: App.versions
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: list.width
                height: 44
                radius: 7
                color: panel.picked === modelData.file ? Theme.accent : (over.containsMouse ? Theme.hover : "transparent")
                Column {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        text: row.modelData.when
                        color: panel.picked === row.modelData.file ? "#ffffff" : Theme.text
                        font.pixelSize: 13
                    }
                    Text {
                        text: (row.index === 0 ? "La más reciente · " : "") + row.modelData.size
                        color: panel.picked === row.modelData.file ? "#d9ffffff" : Theme.dim
                        font.pixelSize: 11
                    }
                }
                MouseArea {
                    id: over
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.pick(row.modelData.file)
                }
            }
        }
        Rectangle {
            visible: list.count > 0
            x: 234
            y: 50
            width: parent.width - x - 18
            height: parent.height - y - 58
            radius: 8
            color: "#ffffff"
            border.color: Theme.border
            clip: true
            Flickable {
                anchors.fill: parent
                anchors.margins: 1
                contentHeight: sample.height + 28
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Text {
                    id: sample
                    x: 18
                    y: 14
                    width: parent.width - 36
                    text: panel.preview === "" ? "(Sin texto)" : panel.preview
                    wrapMode: Text.Wrap
                    lineHeight: 1.3
                    color: "#1d1d1f"
                    font.pixelSize: 12
                }
            }
        }
        Text {
            visible: list.count > 0
            x: 234
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 23
            text: panel.facts.words === undefined ? "" : panel.facts.words + " palabras · " + panel.facts.pages + (panel.facts.pages === 1 ? " página" : " páginas")
            color: Theme.dim
            font.pixelSize: 12
        }
        DialogButton {
            visible: list.count > 0
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            text: "Restaurar esta versión"
            primary: true
            active: panel.picked !== ""
            onClicked: {
                const file = panel.picked
                panel.close()
                panel.restore(file)
            }
        }
    }
}
