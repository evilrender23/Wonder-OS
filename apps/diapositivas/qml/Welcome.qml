import QtQuick
import QtQuick.Controls.Basic

/// What the window shows before there is a presentation: a template to start from, or one to go back to.
Rectangle {
    id: welcome
    signal chosen(string theme, string aspect)
    signal opened(string path)
    signal browse()
    property string aspect: "16:9"
    readonly property var themes: App.deck.themes()
    readonly property var recovered: { App.shelf; return App.recoveries() }
    color: Theme.window

    Flickable {
        anchors.fill: parent
        contentHeight: page.height + 80
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { width: 8 }
        Column {
            id: page
            y: 40
            width: Math.min(parent.width - 80, 940)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 18

            Item {
                width: parent.width
                height: 44
                Image {
                    id: mark
                    width: 44
                    height: 44
                    sourceSize: Qt.size(88, 88)
                    source: "qrc:/qt/qml/WonderlandDiapositivas/wonderland-diapositivas.svg"
                }
                Column {
                    anchors.left: mark.right
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text { text: "Diapositivas"; color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold }
                    Text { text: "Elige un tema para empezar o abre una presentación"; color: Theme.dim; font.pixelSize: 13 }
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Segmented {
                        options: [{key: "16:9", label: "16:9"}, {key: "4:3", label: "4:3"}]
                        current: welcome.aspect
                        onPicked: key => welcome.aspect = key
                    }
                    DialogButton { text: "Abrir…"; primary: true; onClicked: welcome.browse() }
                }
            }

            Repeater {
                model: welcome.recovered
                Rectangle {
                    id: rescue
                    required property var modelData
                    width: page.width
                    height: 46
                    radius: 10
                    color: Theme.wash(Theme.tint)
                    Sym { x: 14; anchors.verticalCenter: parent.verticalCenter; name: "rotate-ccw"; size: 16; color: Theme.tint }
                    Text {
                        x: 40
                        width: parent.width - 60 - buttons.width
                        anchors.verticalCenter: parent.verticalCenter
                        text: "«" + rescue.modelData.name + "» se cerró sin guardar" + (rescue.modelData.date ? " · " + rescue.modelData.date : "")
                        color: Theme.text
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Row {
                        id: buttons
                        anchors.right: parent.right
                        anchors.rightMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        DialogButton { text: "Descartar"; onClicked: App.discardRecovery(rescue.modelData.file) }
                        DialogButton { text: "Recuperar"; primary: true; onClicked: App.recover(rescue.modelData.file) }
                    }
                }
            }

            Text { text: "Plantillas"; color: Theme.dim; font.pixelSize: 11; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.4 }
            Grid {
                id: grid
                readonly property int perRow: Math.max(2, Math.min(4, Math.floor((page.width + 16) / 216)))
                readonly property real cell: (page.width - (perRow - 1) * 16) / perRow
                columns: perRow
                spacing: 16
                Repeater {
                    model: welcome.themes
                    Column {
                        id: card
                        required property var modelData
                        width: grid.cell
                        spacing: 7
                        Rectangle {
                            width: parent.width
                            height: Math.round(width * (welcome.aspect === "4:3" ? 0.75 : 0.5625)) + 4
                            radius: 4
                            color: "transparent"
                            border.color: cardArea.containsMouse ? Theme.tint : Theme.border
                            border.width: cardArea.containsMouse ? 2 : 1
                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                sourceSize.width: 480
                                fillMode: Image.PreserveAspectCrop
                                source: "image://preview/template/" + card.modelData.id
                            }
                            MouseArea {
                                id: cardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: welcome.chosen(card.modelData.id, welcome.aspect)
                            }
                        }
                        Text { text: card.modelData.name; color: Theme.text; font.pixelSize: 13; x: 2 }
                    }
                }
            }

            Item { width: 1; height: 4 }
            Text { text: "Recientes"; color: Theme.dim; font.pixelSize: 11; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.4 }
            Text {
                visible: App.recent.length === 0
                text: "Las presentaciones que abras o guardes aparecerán aquí."
                color: Theme.faint
                font.pixelSize: 13
            }
            Column {
                width: parent.width
                spacing: 2
                Repeater {
                    model: App.recent
                    Rectangle {
                        id: recent
                        required property string modelData
                        width: page.width
                        height: 52
                        radius: 8
                        color: recentArea.containsMouse ? Theme.hover : "transparent"
                        Rectangle {
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 64
                            height: 38
                            radius: 4
                            color: Theme.field
                            border.color: Theme.border
                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                sourceSize.width: 160
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                source: App.previewOf(recent.modelData, 0)
                            }
                        }
                        Column {
                            x: 84
                            width: parent.width - 100
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text { width: parent.width; text: App.fileName(recent.modelData); color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                            Text { width: parent.width; text: App.localPath(App.folderOf(recent.modelData)); color: Theme.dim; font.pixelSize: 12; elide: Text.ElideMiddle }
                        }
                        MouseArea {
                            id: recentArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: welcome.opened(recent.modelData)
                        }
                    }
                }
            }
        }
    }
}
