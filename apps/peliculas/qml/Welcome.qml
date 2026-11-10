import QtQuick
import QtQuick.Controls.Basic

/// The first page: a way in, and what was being watched.
Rectangle {
    id: page
    /// [{path, title, folder, position, duration, progress, finished, audio}]
    property var recent: []
    signal openFiles()
    signal openFolder()
    signal chosen(string path)
    signal forgotten(string path)
    signal cleared()
    color: Theme.page

    function remaining(item) {
        if (item.finished)
            return "Visto"
        if (item.duration <= 0 || item.position <= 0)
            return item.duration > 0 ? App.time(item.duration) : ""
        const minutes = Math.round((item.duration - item.position) / 60000)
        return minutes < 1 ? "Queda menos de 1 min" : (minutes === 1 ? "Queda 1 min" : "Quedan " + minutes + " min")
    }

    Column {
        id: hero
        anchors.horizontalCenter: parent.horizontalCenter
        y: page.recent.length > 0 ? Math.max(28, Math.round(page.height * 0.09)) : Math.round((page.height - height) / 2 - 10)
        spacing: 8
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            width: page.recent.length > 0 ? 64 : 96
            height: width
            sourceSize: Qt.size(192, 192)
            source: "qrc:/qt/qml/WonderlandPeliculas/wonderland-peliculas.svg"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Películas"
            color: Theme.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Arrastra aquí un vídeo o una carpeta, o ábrelo."
            color: Theme.dim
            font.pixelSize: 13
        }
        Item { width: 1; height: 6 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            PillButton {
                symbol: "film"
                text: "Abrir archivo…"
                onClicked: page.openFiles()
            }
            PillButton {
                symbol: "folder-open"
                text: "Abrir carpeta…"
                onClicked: page.openFolder()
            }
        }
    }

    Item {
        visible: page.recent.length > 0
        anchors.top: hero.bottom
        anchors.topMargin: 26
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(620, parent.width - 48)

        Text {
            id: heading
            text: "Continuar viendo"
            color: Theme.dim
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        Text {
            anchors.right: parent.right
            anchors.baseline: heading.baseline
            text: "Borrar historial"
            color: clearArea.containsMouse ? Theme.tint : Theme.dim
            font.pixelSize: 12
            MouseArea {
                id: clearArea
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.cleared()
            }
        }
        ListView {
            id: list
            anchors.top: heading.bottom
            anchors.topMargin: 8
            anchors.bottom: parent.bottom
            width: parent.width
            clip: true
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            model: page.recent
            ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
            delegate: Rectangle {
                id: row
                required property var modelData
                width: list.width
                height: 52
                radius: 9
                color: rowArea.containsMouse ? Theme.sideSelection : Theme.card
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: page.chosen(row.modelData.path)
                }
                Rectangle {
                    id: badge
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    radius: 8
                    color: Theme.field
                    Sym {
                        anchors.centerIn: parent
                        name: row.modelData.audio ? "music" : "film"
                        size: 16
                        color: Theme.tint
                    }
                }
                Column {
                    anchors.left: badge.right
                    anchors.leftMargin: 11
                    anchors.right: forget.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        width: parent.width
                        text: row.modelData.title
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        elide: Text.ElideMiddle
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.modelData.finished && row.modelData.progress > 0
                            width: 72
                            height: 3
                            radius: 2
                            color: Theme.field
                            Rectangle {
                                width: Math.max(3, parent.width * row.modelData.progress)
                                height: parent.height
                                radius: 2
                                color: Theme.tint
                            }
                        }
                        Text {
                            width: parent.width - (parent.children[0].visible ? 80 : 0)
                            text: [page.remaining(row.modelData), row.modelData.folder].filter(part => part !== "").join("  ·  ")
                            color: Theme.dim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
                TButton {
                    id: forget
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "x"
                    size: 13
                    tone: Theme.dim
                    tip: "Quitar del historial"
                    opacity: rowArea.containsMouse || hovered ? 1 : 0
                    onClicked: page.forgotten(row.modelData.path)
                }
            }
        }
    }
}
