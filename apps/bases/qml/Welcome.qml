import QtQuick
import QtQuick.Controls.Basic

/// What the window shows with no database open: make one, or open one.
Item {
    id: page
    signal create(string templateId)
    signal browse()
    signal sample()

    Flickable {
        anchors.fill: parent
        contentHeight: Math.max(height, body.height + 80)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: body
            y: Math.max(40, (parent.height - height) / 2)
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(page.width - 64, 760)
            spacing: 22
            Row {
                spacing: 14
                Image {
                    source: "wonderland-bases.svg"
                    sourceSize: Qt.size(56, 56)
                    width: 56
                    height: 56
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: "Bases"
                        color: Theme.text
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "Tablas, consultas, formularios e informes sobre SQLite"
                        color: Theme.dim
                        font.pixelSize: 13
                    }
                }
            }
            Row {
                spacing: 8
                TextButton {
                    symbol: "plus"
                    text: "Base en blanco"
                    strong: true
                    onClicked: page.create("")
                }
                TextButton {
                    symbol: "folder-open"
                    text: "Abrir…"
                    color: Theme.field
                    onClicked: page.browse()
                }
                TextButton {
                    symbol: "database"
                    text: "Base de ejemplo"
                    color: Theme.field
                    onClicked: page.sample()
                }
            }
            Column {
                width: parent.width
                spacing: 8
                Caption { text: "Plantillas" }
                Row {
                    id: cards
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: App.templates
                        Rectangle {
                            id: card
                            required property var modelData
                            width: (cards.width - 20) / 3
                            height: 104
                            radius: 10
                            color: cardArea.containsMouse ? Theme.sideSelection : Theme.field
                            Sym {
                                x: 14
                                y: 14
                                name: card.modelData.symbol
                                size: 20
                                color: Theme.tint
                            }
                            Text {
                                x: 14
                                y: 42
                                width: parent.width - 28
                                text: card.modelData.name
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                x: 14
                                y: 62
                                width: parent.width - 28
                                text: card.modelData.description
                                color: Theme.dim
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: cardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: page.create(card.modelData.id)
                            }
                        }
                    }
                }
            }
            Column {
                width: parent.width
                spacing: 4
                visible: App.recent.length > 0
                Caption {
                    text: "Recientes"
                    bottomPadding: 4
                }
                Repeater {
                    model: App.recent.slice(0, 6)
                    Rectangle {
                        id: recent
                        required property var modelData
                        width: parent.width
                        height: 40
                        radius: 8
                        color: recentArea.containsMouse ? Theme.hover : "transparent"
                        Sym {
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            name: "database"
                            size: 16
                            color: Theme.dim
                        }
                        Text {
                            x: 36
                            y: 4
                            width: parent.width - 80
                            text: recent.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Text {
                            x: 36
                            y: 21
                            width: parent.width - 80
                            text: recent.modelData.folder
                            color: Theme.faint
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        MouseArea {
                            id: recentArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: App.openPath(recent.modelData.path)
                        }
                        TButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            visible: recentArea.containsMouse || hovering.hovered
                            symbol: "x"
                            size: 13
                            tone: Theme.dim
                            onClicked: App.forgetRecent(recent.modelData.path)
                            HoverHandler { id: hovering }
                        }
                    }
                }
            }
        }
    }
}
