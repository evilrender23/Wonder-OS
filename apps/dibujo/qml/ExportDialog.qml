import QtQuick
import QtQuick.Controls.Basic

/// What goes out and how: the format, the frame, the size.
Popup {
    id: dialog
    property string format: "png"
    property string frame: ""
    property real factor: 2
    property bool clear: false
    signal accepted(string format, string frame, real scale, bool transparent)

    function show(kind) {
        format = kind
        // A selected frame is what one most likely means.
        const info = App.editor.info
        frame = info.isFrame && info.single ? info.id : ""
        open()
    }

    readonly property var areas: [{key: "", label: App.editor.frames.length > 0 && format === "pdf" ? "Todos los marcos" : "Todo el dibujo"}]
        .concat(App.editor.frames.map(f => ({key: f.id, label: f.name + "  ·  " + f.size})))

    modal: true
    anchors.centerIn: parent
    width: 400
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: "#40000000" }
    contentItem: Column {
        spacing: 8
        Text {
            text: "Exportar"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Item { width: 1; height: 2 }
        PropRow {
            label: "Formato"
            Segmented {
                options: [{key: "png", label: "PNG"}, {key: "svg", label: "SVG"}, {key: "pdf", label: "PDF"}]
                current: dialog.format
                onPicked: key => dialog.format = key
            }
        }
        PropRow {
            label: "Qué"
            FCombo {
                width: parent.width
                model: dialog.areas.map(a => a.label)
                currentIndex: Math.max(0, dialog.areas.findIndex(a => a.key === dialog.frame))
                onActivated: index => dialog.frame = dialog.areas[index].key
            }
        }
        PropRow {
            label: "Escala"
            visible: dialog.format === "png"
            Segmented {
                options: [{key: "1", label: "1×"}, {key: "2", label: "2×"}, {key: "3", label: "3×"}, {key: "4", label: "4×"}]
                current: String(dialog.factor)
                onPicked: key => dialog.factor = Number(key)
            }
        }
        PropRow {
            label: "Fondo"
            visible: dialog.format === "png"
            Check {
                anchors.verticalCenter: parent.verticalCenter
                text: "Transparente"
                on: dialog.clear
                onToggled: on => dialog.clear = on
            }
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            topPadding: 4
            text: dialog.format === "png" ? "Una imagen de píxeles, a la escala elegida."
                : dialog.format === "svg" ? "Vectores que abren Inkscape, los navegadores y LibreOffice. El texto sigue siendo texto."
                : dialog.frame === "" && App.editor.frames.length > 0 ? "Un PDF vectorial con una página por marco, cada una de su tamaño."
                : "Un PDF vectorial de una página."
            color: Theme.dim
            font.pixelSize: 12
        }
        Item { width: 1; height: 4 }
        Row {
            anchors.right: parent.right
            spacing: 8
            DialogButton {
                text: "Cancelar"
                onClicked: dialog.close()
            }
            DialogButton {
                text: "Exportar…"
                primary: true
                onClicked: {
                    dialog.close()
                    dialog.accepted(dialog.format, dialog.frame, dialog.factor, dialog.clear)
                }
            }
        }
    }
}
