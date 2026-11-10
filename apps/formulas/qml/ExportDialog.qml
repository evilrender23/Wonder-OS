import QtQuick
import QtQuick.Controls.Basic

/// What goes out, and how.
Popup {
    id: dialog
    property string format: "png"
    property real factor: 2
    property bool transparent: true
    signal accepted(string format, real scale, bool transparent)

    readonly property var notes: ({
        png: "La ecuación en curso como imagen.",
        svg: "La ecuación en curso en vectores: solo trazados, no necesita la fuente donde se abra.",
        pdf: "Todas las ecuaciones del documento, numeradas, en páginas A4.",
        mml: "La ecuación en curso en MathML de presentación, con el LaTeX como anotación.",
        odf: "El documento como fórmula de OpenDocument, para LibreOffice Math.",
        tex: "El documento como fragmento de LaTeX: cada ecuación entre \\[ y \\]."
    })

    function show(kind) {
        format = kind
        open()
    }

    modal: true
    anchors.centerIn: parent
    width: 440
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: "#40000000" }
    contentItem: Column {
        spacing: 12
        Text {
            text: "Exportar"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Segmented {
            options: [{key: "png", label: "PNG"}, {key: "svg", label: "SVG"}, {key: "pdf", label: "PDF"}, {key: "mml", label: "MathML"}, {key: "odf", label: "ODF"}, {key: "tex", label: "LaTeX"}]
            current: dialog.format
            onPicked: key => dialog.format = key
        }
        Text {
            width: parent.width
            height: 34
            wrapMode: Text.Wrap
            text: dialog.notes[dialog.format]
            color: Theme.dim
            font.pixelSize: 12
        }
        Item {
            width: parent.width
            height: 64
            Column {
                visible: dialog.format === "png"
                width: parent.width
                spacing: 8
                PropRow {
                    label: "Escala"
                    Segmented {
                        options: [{key: "1", label: "1×"}, {key: "2", label: "2×"}, {key: "3", label: "3×"}, {key: "4", label: "4×"}, {key: "8", label: "8×"}]
                        current: String(dialog.factor)
                        onPicked: key => dialog.factor = Number(key)
                    }
                }
                PropRow {
                    label: "Fondo"
                    Check {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Transparente"
                        on: dialog.transparent
                        onToggled: on => dialog.transparent = on
                    }
                }
            }
        }
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
                    dialog.accepted(dialog.format, dialog.factor, dialog.transparent)
                }
            }
        }
    }
}
