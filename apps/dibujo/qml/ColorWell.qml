import QtQuick
import QtQuick.Controls.Basic

/// A colour: press it for the colours of the drawing, or type one.
Rectangle {
    id: well
    /// "#rrggbb" or "#aarrggbb".
    property string value: "#000000"
    /// The selected differ: no one colour to show.
    property bool mixed: false
    signal picked(string color)

    function open() {
        hex.text = value
        popup.open()
    }

    width: 44
    height: 24
    radius: 6
    color: Theme.field
    Rectangle {
        anchors.fill: parent
        anchors.margins: 3
        radius: 4
        color: well.mixed ? Theme.field : well.value
        border.color: Theme.dark ? "#30ffffff" : "#24000000"
        Text {
            visible: well.mixed
            anchors.centerIn: parent
            text: "—"
            color: Theme.dim
            font.pixelSize: 12
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: well.open()
    }
    component Swatches: Grid {
        property var colors: []
        columns: 8
        spacing: 4
        Repeater {
            model: parent.colors
            Rectangle {
                required property string modelData
                width: 20
                height: 20
                radius: 5
                color: modelData
                border.color: Theme.dark ? "#30ffffff" : "#24000000"
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        well.picked(parent.modelData)
                        popup.close()
                    }
                }
            }
        }
    }
    Popup {
        id: popup
        y: well.height + 4
        x: Math.min(0, well.width - width)
        width: 212
        padding: 10
        background: Rectangle {
            radius: 10
            color: Theme.menu
            border.color: Theme.border
        }
        contentItem: Column {
            spacing: 8
            Text {
                text: "Colores del dibujo"
                color: Theme.dim
                font.pixelSize: 11
            }
            Swatches {
                colors: popup.visible ? App.editor.palette : []
            }
            Swatches {
                colors: ["#000000", "#48484a", "#8e8e93", "#c7c7cc", "#ffffff", "#ff3b30", "#ff9500", "#ffcc00",
                         "#34c759", "#00c7be", "#0f9aa8", "#0a7aff", "#5856d6", "#af52de", "#ff2d55", "#a2845e"]
            }
            Row {
                spacing: 6
                Field {
                    id: hex
                    width: 104
                    placeholderText: "#rrggbb"
                    wrong: text !== "" && !/^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text)
                    onAccepted: {
                        if (!wrong && text !== "") {
                            well.picked(text.toLowerCase())
                            popup.close()
                        }
                    }
                }
                DialogButton {
                    implicitWidth: 82
                    text: "Guardar"
                    onClicked: App.editor.addPaletteColor(well.value)
                }
            }
        }
    }
}
