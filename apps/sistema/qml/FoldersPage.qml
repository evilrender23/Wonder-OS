import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

/// The colour of the desktop's folders. A click only shows it on the folder here; «Aplicar»
/// is what changes the Desktop and the file manager.
ScrollView {
    id: root

    required property var page

    /// The colour being looked at: "#rrggbb", or empty for the usual blue.
    property string pending: Appearance.folderTint
    readonly property bool dirty: pending !== Appearance.folderTint
    readonly property bool custom: pending !== "" && !colors.some(c => c[0] === pending)

    readonly property var colors: [
        ["", "Azul (de fábrica)"], ["#ff453a", "Rojo"], ["#ff9f0a", "Naranja"], ["#ffd60a", "Amarillo"], ["#32d74b", "Verde"],
        ["#40c8e0", "Turquesa"], ["#0a84ff", "Azul intenso"], ["#bf5af2", "Morado"], ["#ff6fae", "Rosa"], ["#ac8e68", "Marrón"], ["#98989d", "Gris"]
    ]

    function pick(color) {
        const clean = color === "" ? "" : Appearance.cleanColor(color)
        if (color !== "" && clean === "")
            return false
        pending = clean
        return true
    }
    function apply() {
        return Appearance.setFolderTint(pending)
    }
    function discard() {
        pending = Appearance.folderTint
    }

    contentWidth: availableWidth
    clip: true

    Connections {
        target: Appearance

        // Changed from elsewhere (another Sistema, a hand edit): show what is in use now.
        function onFolderTintChanged() {
            root.pending = Appearance.folderTint
        }
    }
    Component.onCompleted: {
        if (startTint !== "")
            pick(startTint)
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 0

        Text {
            Layout.leftMargin: 28
            Layout.topMargin: 22
            Layout.bottomMargin: 4
            text: root.page.title
            color: Theme.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 16
            spacing: 6

            Text {
                Layout.leftMargin: 4
                text: "Color de las carpetas"
                color: Theme.text
                font.pixelSize: Theme.body
                font.weight: Font.DemiBold
            }
            Text {
                Layout.leftMargin: 4
                Layout.fillWidth: true
                text: "El color de las carpetas en el Escritorio y en Rabbit. Las que tienen un color propio («Personalizar carpeta…») lo conservan."
                color: Theme.dim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: card.implicitHeight + 36
                radius: Theme.radius
                color: Theme.card
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    id: card

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 22
                    anchors.rightMargin: 18
                    spacing: 24

                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: 2

                        FolderArt {
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 112
                            implicitHeight: 112
                            tint: root.pending
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: root.dirty ? "Vista previa" : "En uso"
                            color: Theme.dim
                            font.pixelSize: Theme.small
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 14

                        Flow {
                            Layout.fillWidth: true
                            Layout.leftMargin: -4
                            spacing: 8

                            Repeater {
                                model: root.colors

                                Rectangle {
                                    id: swatch

                                    required property var modelData
                                    readonly property bool on: root.pending === modelData[0]

                                    width: 30
                                    height: 30
                                    radius: 15
                                    color: "transparent"
                                    border.width: on ? 2 : 0
                                    border.color: Theme.accent

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 22
                                        height: 22
                                        radius: 11
                                        color: swatch.modelData[0] === "" ? "#3fa4f1" : swatch.modelData[0]
                                        border.width: 1
                                        border.color: Theme.dark ? "#33ffffff" : "#22000000"
                                    }
                                    HoverHandler {
                                        id: swatchHover
                                    }
                                    TapHandler {
                                        onTapped: root.pick(swatch.modelData[0])
                                    }
                                    ToolTip.visible: swatchHover.hovered
                                    ToolTip.delay: 600
                                    ToolTip.text: swatch.modelData[1]
                                }
                            }
                        }

                        RowLayout {
                            spacing: 8

                            Text {
                                text: "Color personalizado"
                                color: Theme.text
                                font.pixelSize: Theme.body
                            }
                            Rectangle {
                                implicitWidth: 30
                                implicitHeight: 30
                                radius: 7
                                color: root.pending === "" ? "#3fa4f1" : root.pending
                                border.width: root.custom ? 2 : 1
                                border.color: root.custom ? Theme.accent : Theme.border

                                TapHandler {
                                    onTapped: {
                                        colorDialog.selectedColor = root.pending === "" ? "#3fa4f1" : root.pending
                                        colorDialog.open()
                                    }
                                }
                            }
                            TField {
                                id: hexField

                                implicitWidth: 110
                                placeholderText: "#rrggbb"
                                text: root.pending
                                onEditingFinished: {
                                    // Typing broke the binding; what isn't a colour goes back to the one shown.
                                    root.pick(text.trim())
                                    text = Qt.binding(() => root.pending)
                                }
                            }
                            TButton {
                                text: "Elegir…"
                                onClicked: {
                                    colorDialog.selectedColor = root.pending === "" ? "#3fa4f1" : root.pending
                                    colorDialog.open()
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: Appearance.error
                            color: Theme.danger
                            font.pixelSize: Theme.small
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            spacing: 8

                            Item {
                                Layout.fillWidth: true
                            }
                            TButton {
                                text: "Descartar"
                                enabled: root.dirty
                                onClicked: root.discard()
                            }
                            TButton {
                                text: "Aplicar"
                                primary: true
                                enabled: root.dirty
                                onClicked: root.apply()
                            }
                        }
                    }
                }
            }
        }

        Item {
            implicitHeight: 28
        }
    }

    ColorDialog {
        id: colorDialog

        title: "Color de las carpetas"
        onAccepted: {
            const c = selectedColor
            const hex = n => Math.round(n * 255).toString(16).padStart(2, "0")
            root.pick("#" + hex(c.r) + hex(c.g) + hex(c.b))
        }
    }
}
