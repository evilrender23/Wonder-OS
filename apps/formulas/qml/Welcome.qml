import QtQuick
import QtQuick.Controls.Basic

/// What the window shows with no document in it: where to start from.
Rectangle {
    id: welcome
    signal openRequested()
    color: Theme.window

    component Card: Item {
        id: card
        property string name
        property string detail
        property string latex
        property bool blank: false
        signal clicked()
        width: cards.cell
        // Room for a name of two lines, so that it never reaches the row below.
        height: thumb.height + 46
        Rectangle {
            id: thumb
            width: parent.width
            height: Math.round(width * 0.6)
            radius: 10
            color: Theme.dark ? "#2a2a2c" : "#f5f5f8"
            border.color: hit.containsMouse ? Theme.tint : Theme.border
            border.width: hit.containsMouse ? 2 : 1
            Thumb {
                anchors.fill: parent
                anchors.margins: 12
                visible: !card.blank
                latex: card.latex
                size: 17
            }
            Sym {
                anchors.centerIn: parent
                visible: card.blank
                name: "plus"
                size: 26
                color: Theme.faint
            }
        }
        Text {
            y: thumb.height + 8
            width: parent.width
            elide: Text.ElideRight
            wrapMode: Text.Wrap
            maximumLineCount: card.detail === "" ? 2 : 1
            text: card.name
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Text {
            y: thumb.height + 25
            width: parent.width
            elide: Text.ElideRight
            text: card.detail
            color: Theme.dim
            font.pixelSize: 11
        }
        MouseArea {
            id: hit
            anchors.fill: parent
            hoverEnabled: true
            onClicked: card.clicked()
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: page.height + 80
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        function reveal(y) {
            contentY = Math.max(0, Math.min(contentHeight - height, y))
        }
        Column {
            id: page
            y: 44
            width: Math.min(parent.width - 80, 880)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14

            Item {
                width: parent.width
                height: 44
                Image {
                    id: icon
                    width: 40
                    height: 40
                    sourceSize: Qt.size(80, 80)
                    source: "qrc:/qt/qml/WonderlandFormulas/wonderland-formulas.svg"
                }
                Column {
                    x: 54
                    anchors.verticalCenter: icon.verticalCenter
                    Text {
                        text: "Fórmulas"
                        color: Theme.text
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "Matemáticas en LaTeX, a la vista mientras se escriben"
                        color: Theme.dim
                        font.pixelSize: 12
                    }
                }
                DialogButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Abrir…"
                    onClicked: welcome.openRequested()
                }
            }

            // Work a run that ended badly left behind.
            Rectangle {
                visible: App.recoveries.length > 0
                width: parent.width
                height: visible ? 46 : 0
                radius: 10
                color: Theme.field
                Sym {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    name: "history"
                    color: Theme.tint
                }
                Text {
                    x: 40
                    width: parent.width - 300
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: App.recoveries.length > 0 ? "Hay cambios sin guardar de «" + App.recoveries[0].title + "» (" + App.recoveries[0].when + ")" : ""
                    color: Theme.text
                    font.pixelSize: 13
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    DialogButton {
                        text: "Descartar"
                        onClicked: App.dropRecoveries()
                    }
                    DialogButton {
                        text: "Recuperar"
                        primary: true
                        onClicked: App.recover(App.recoveries[0].path)
                    }
                }
            }

            Heading { text: "Empezar con" }
            Grid {
                id: cards
                readonly property int gap: 14
                columns: Math.max(2, Math.min(5, Math.floor((page.width + gap) / (160 + gap))))
                readonly property real cell: Math.floor((page.width - gap * (columns - 1)) / columns)
                width: page.width
                spacing: gap
                Repeater {
                    model: App.templates()
                    Card {
                        required property var modelData
                        name: modelData.name
                        detail: modelData.detail
                        latex: modelData.latex
                        blank: modelData.key === "blank"
                        onClicked: App.newDocument(modelData.key)
                    }
                }
            }

            Item { width: 1; height: 6 }
            Heading { text: "Recientes" }
            Text {
                visible: App.recents.length === 0
                text: "Los documentos que guardes aparecerán aquí."
                color: Theme.dim
                font.pixelSize: 13
            }
            Column {
                width: parent.width
                Repeater {
                    model: App.recents
                    Rectangle {
                        id: recent
                        required property var modelData
                        width: parent.width
                        height: 52
                        radius: 9
                        color: row.containsMouse ? Theme.hover : "transparent"
                        MouseArea {
                            id: row
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: App.openPath(recent.modelData.path)
                        }
                        Rectangle {
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 56
                            height: 38
                            radius: 5
                            color: Theme.dark ? "#2a2a2c" : "#f5f5f8"
                            border.color: Theme.border
                            Image {
                                anchors.fill: parent
                                anchors.margins: 3
                                fillMode: Image.PreserveAspectFit
                                sourceSize: Qt.size(150, 96)
                                source: "image://formula/file/" + String(Theme.text).slice(1) + "/" + recent.modelData.path
                            }
                        }
                        Text {
                            x: 76
                            y: 9
                            width: parent.width - 250
                            elide: Text.ElideRight
                            text: recent.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                        Text {
                            x: 76
                            y: 28
                            width: parent.width - 250
                            elide: Text.ElideMiddle
                            text: recent.modelData.folder
                            color: Theme.dim
                            font.pixelSize: 11
                        }
                        Text {
                            anchors.right: forget.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: recent.modelData.when
                            color: Theme.dim
                            font.pixelSize: 11
                        }
                        TButton {
                            id: forget
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            symbol: "x"
                            size: 13
                            tone: Theme.dim
                            opacity: row.containsMouse ? 1 : 0
                            onClicked: App.forgetRecent(recent.modelData.path)
                        }
                    }
                }
            }

            // The collection that comes with the app, and what the user kept.
            Repeater {
                model: {
                    App.libraryRevision
                    return App.library("").filter(s => s.entries.length > 0)
                }
                Column {
                    id: shelf
                    required property var modelData
                    width: page.width
                    spacing: 14
                    Item { width: 1; height: 1 }
                    Heading { text: shelf.modelData.own ? shelf.modelData.title : "Colección · " + shelf.modelData.title }
                    Grid {
                        width: page.width
                        columns: cards.columns
                        columnSpacing: cards.gap
                        rowSpacing: cards.gap
                        Repeater {
                            model: shelf.modelData.entries
                            Card {
                                required property var modelData
                                name: modelData.name
                                latex: modelData.latex
                                onClicked: App.newDocument(modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
    function scrollTo(y) {
        flick.reveal(y)
    }
}
