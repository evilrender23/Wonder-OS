import QtQuick
import QtQuick.Controls.Basic

/// What the window shows before it has a document: templates, and what was open lately.
Rectangle {
    id: welcome
    signal picked(string kind)
    signal opened(string path)
    signal browse()
    color: App.glass ? Theme.sidebar : Theme.window

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 72
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }
        Column {
            id: column
            y: 36
            width: Math.min(820, welcome.width - 64)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14

            Item {
                width: parent.width
                height: 48
                Image {
                    id: mark
                    width: 44
                    height: 44
                    anchors.verticalCenter: parent.verticalCenter
                    sourceSize: Qt.size(88, 88)
                    source: "qrc:/qt/qml/WonderlandTextos/wonderland-textos.svg"
                }
                Column {
                    x: 58
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        text: "Textos"
                        color: Theme.text
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "Empieza una hoja nueva o sigue donde lo dejaste"
                        color: Theme.dim
                        font.pixelSize: 13
                    }
                }
                DialogButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Abrir…"
                    onClicked: welcome.browse()
                }
            }

            Item { width: 1; height: 4 }
            SectionLabel {
                text: "Documento nuevo"
            }
            Row {
                id: cards
                readonly property real card: Math.floor((parent.width - 4 * spacing) / 5)
                spacing: 16
                Repeater {
                    model: App.templates
                    Column {
                        id: card
                        required property var modelData
                        width: cards.card
                        spacing: 7
                        Rectangle {
                            width: parent.width
                            height: Math.round(width * 1.32)
                            radius: 6
                            color: "#ffffff"
                            border.color: area.containsMouse ? Theme.tint : Theme.border
                            border.width: area.containsMouse ? 2 : 1
                            clip: true
                            Image {
                                x: 2
                                y: 2
                                width: parent.width - 4
                                height: parent.height - 4
                                fillMode: Image.PreserveAspectCrop
                                verticalAlignment: Image.AlignTop
                                sourceSize.width: Math.round(width * 2)
                                source: "image://template/" + card.modelData.id
                                smooth: true
                                mipmap: true
                            }
                            MouseArea {
                                id: area
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: welcome.picked(card.modelData.id)
                            }
                        }
                        Text {
                            width: parent.width
                            text: card.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            topPadding: -5
                            text: card.modelData.detail
                            color: Theme.dim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Item { width: 1; height: 4; visible: recovered.count > 0 }
            SectionLabel {
                visible: recovered.count > 0
                text: "Sin guardar de la última vez"
            }
            Column {
                width: parent.width
                visible: recovered.count > 0
                Repeater {
                    id: recovered
                    model: App.recovered
                    Rectangle {
                        id: lost
                        required property var modelData
                        width: parent.width
                        height: 44
                        radius: 8
                        color: Theme.field
                        Sym {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            name: "history"
                            size: 17
                            color: Theme.tint
                        }
                        Text {
                            x: 40
                            anchors.verticalCenter: parent.verticalCenter
                            text: lost.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                rightPadding: 6
                                text: lost.modelData.when
                                color: Theme.dim
                                font.pixelSize: 12
                            }
                            DialogButton {
                                text: "Descartar"
                                onClicked: App.discardRecovered(lost.modelData.file)
                            }
                            DialogButton {
                                text: "Recuperar"
                                primary: true
                                onClicked: App.openRecovered(lost.modelData.file)
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 4 }
            SectionLabel {
                text: "Recientes"
            }
            Column {
                width: parent.width
                Repeater {
                    id: recents
                    model: App.recents
                    Rectangle {
                        id: recent
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: 40
                        radius: 8
                        color: over.containsMouse ? Theme.hover : "transparent"
                        Rectangle {
                            visible: recent.index > 0 && !over.containsMouse
                            x: 40
                            width: parent.width - 40
                            height: 1
                            color: Theme.line
                        }
                        Sym {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            name: "file-text"
                            size: 17
                            color: Theme.tint
                        }
                        Text {
                            id: name
                            x: 40
                            width: Math.min(implicitWidth, parent.width * 0.45)
                            anchors.verticalCenter: parent.verticalCenter
                            text: recent.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Text {
                            anchors.left: name.right
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "." + recent.modelData.kind
                            color: Theme.faint
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.right: when.left
                            anchors.rightMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, parent.width * 0.28)
                            text: recent.modelData.folder
                            color: Theme.dim
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }
                        Text {
                            id: when
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 128
                            horizontalAlignment: Text.AlignRight
                            text: recent.modelData.when
                            color: Theme.dim
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: over
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: welcome.opened(recent.modelData.path)
                        }
                    }
                }
                Text {
                    visible: recents.count === 0
                    topPadding: 6
                    text: "Los documentos que abras o guardes aparecerán aquí."
                    color: Theme.dim
                    font.pixelSize: 13
                }
            }
        }
    }
}
