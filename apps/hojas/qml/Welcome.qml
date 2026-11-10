import QtQuick
import QtQuick.Controls.Basic

/// What there is before a document: the templates, and the files lately open.
Rectangle {
    id: welcome
    required property var host
    readonly property var book: host.book
    readonly property var templates: [
        {kind: "blank", name: "En blanco"},
        {kind: "budget", name: "Presupuesto personal"},
        {kind: "invoice", name: "Factura"},
        {kind: "tasks", name: "Lista de tareas"},
        {kind: "schedule", name: "Horario"},
    ]
    property var recoveries: book.recoveries()
    color: Theme.window

    // Nothing behind it is reached.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: event => event.accepted = true }

    Flickable {
        anchors.fill: parent
        contentHeight: page.height + 56
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: page
            readonly property real card: Math.floor((width - 4 * 16) / 5)
            x: Math.round((parent.width - width) / 2)
            y: 34
            width: Math.min(880, welcome.width - 64)
            spacing: 14

            Item {
                width: parent.width
                height: 44
                Image {
                    id: logo
                    width: 40
                    height: 40
                    sourceSize: Qt.size(80, 80)
                    source: "qrc:/qt/qml/WonderlandHojas/wonderland-hojas.svg"
                }
                Column {
                    x: 52
                    anchors.verticalCenter: logo.verticalCenter
                    Text { text: "Hojas"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
                    Text { text: "Empieza con una plantilla o abre una hoja de cálculo."; color: Theme.dim; font.pixelSize: 12 }
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: logo.verticalCenter
                    spacing: 8
                    DialogButton { text: "Órdenes"; onClicked: welcome.host.run("palette") }
                    DialogButton { text: "Abrir…"; primary: true; onClicked: welcome.host.run("open") }
                }
            }

            // Work that was not saved when Hojas last went away.
            Repeater {
                model: welcome.recoveries
                Rectangle {
                    required property var modelData
                    width: page.width
                    height: 44
                    radius: 9
                    color: Qt.alpha(Theme.tint, Theme.dark ? 0.16 : 0.10)
                    Sym { x: 12; anchors.verticalCenter: parent.verticalCenter; name: "history"; color: Theme.tint }
                    Text {
                        x: 38
                        width: parent.width - 38 - actions.width - 20
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        text: "«" + parent.modelData.title + "» se cerró sin guardar · " + parent.modelData.when
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    Row {
                        id: actions
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        DialogButton {
                            text: "Descartar"
                            onClicked: {
                                welcome.book.discardRecovery(parent.parent.modelData.file)
                                welcome.recoveries = welcome.book.recoveries()
                            }
                        }
                        DialogButton { text: "Recuperar"; primary: true; onClicked: welcome.host.done(welcome.book.recover(parent.parent.modelData.file)) }
                    }
                }
            }

            Text { topPadding: 6; text: "Plantillas"; color: Theme.dim; font.pixelSize: 12; font.weight: Font.DemiBold }
            Row {
                spacing: 16
                Repeater {
                    model: welcome.templates
                    Item {
                        id: card
                        required property var modelData
                        width: page.card
                        height: thumb.height + 30
                        Rectangle {
                            id: thumb
                            width: parent.width
                            height: Math.round(width * 0.78)
                            radius: 9
                            color: "#ffffff"
                            border.color: cardArea.containsMouse ? Theme.tint : Theme.border
                            border.width: cardArea.containsMouse ? 2 : 1
                            clip: true
                            MiniSheet {
                                anchors.fill: parent
                                anchors.margins: 2
                                radius: 7
                                rows: welcome.book.templatePreview(card.modelData.kind)
                                rowHeight: (height - 12) / 11
                                fontSize: Math.max(4.5, Math.min(6.5, width / 30))
                            }
                        }
                        Text {
                            anchors.top: thumb.bottom
                            anchors.topMargin: 8
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: card.modelData.name
                            color: Theme.text
                            font.pixelSize: 12
                        }
                        MouseArea {
                            id: cardArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: welcome.host.run("new", card.modelData.kind)
                        }
                    }
                }
            }

            Text { topPadding: 10; text: "Recientes"; color: Theme.dim; font.pixelSize: 12; font.weight: Font.DemiBold }
            Text {
                visible: App.recents.length === 0
                text: "Aquí aparecerán las hojas de cálculo que abras o guardes."
                color: Theme.faint
                font.pixelSize: 13
            }
            Column {
                width: parent.width
                Repeater {
                    model: App.recents
                    Rectangle {
                        id: recent
                        required property var modelData
                        width: page.width
                        height: 40
                        radius: 8
                        color: recentArea.containsMouse ? Theme.hover : "transparent"
                        MouseArea {
                            id: recentArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: welcome.host.openFile(recent.modelData.path)
                        }
                        Sym { x: 10; anchors.verticalCenter: parent.verticalCenter; name: "file-spreadsheet"; color: Theme.tint; size: 18 }
                        Text {
                            id: recentName
                            x: 40
                            width: Math.min(implicitWidth, parent.width * 0.4)
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: recent.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                        }
                        Text {
                            x: recentName.x + recentName.width + 10
                            width: Math.max(0, recentWhen.x - x - 12)
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideMiddle
                            text: recent.modelData.kind + " · " + recent.modelData.where
                            color: Theme.faint
                            font.pixelSize: 12
                        }
                        Text {
                            id: recentWhen
                            anchors.right: forget.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: recent.modelData.when
                            color: Theme.dim
                            font.pixelSize: 12
                        }
                        TButton {
                            id: forget
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 22
                            size: 13
                            symbol: "x"
                            tone: Theme.faint
                            onClicked: App.forgetRecent(recent.modelData.path)
                        }
                    }
                }
            }
        }
    }
}
