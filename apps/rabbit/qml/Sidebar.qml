import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: side
    required property var win
    color: Theme.sidebar

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        clip: true
        model: App.places
        boundsBehavior: Flickable.StopAtBounds
        section.property: "section"
        section.delegate: Text {
            required property string section
            text: section
            color: Theme.dim
            font.pixelSize: 11
            font.bold: true
            leftPadding: 16
            topPadding: 12
            bottomPadding: 4
        }
        delegate: Item {
            id: row
            required property string title
            required property url url
            required property string symbol
            required property string kind
            required property string color
            required property string udi
            required property bool ejectable
            required property bool mounted
            readonly property bool current: mounted && String(url) === String(side.win.location)
            width: list.width
            height: 28

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                radius: 6
                color: drop.containsDrag ? Theme.accent : (row.current ? Theme.sideSelection : (area.containsMouse ? Theme.hover : "transparent"))
            }
            Rectangle {
                visible: row.kind === "tag"
                x: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 11
                height: 11
                radius: 6
                color: row.color
            }
            Sym {
                visible: row.kind !== "tag"
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                name: row.symbol
                color: drop.containsDrag ? "white" : Theme.accent
            }
            Text {
                x: 40
                width: parent.width - x - (eject.visible ? 34 : 14)
                anchors.verticalCenter: parent.verticalCenter
                text: row.title
                elide: Text.ElideRight
                font.pixelSize: 13
                color: drop.containsDrag ? "white" : (row.mounted ? Theme.text : Theme.dim)
            }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        menu.popup()
                    } else if (!row.mounted) {
                        App.places.mount(row.udi)
                    } else if (mouse.button === Qt.MiddleButton || (mouse.modifiers & Qt.ControlModifier)) {
                        App.requestWindow(row.url)
                    } else {
                        side.win.go(row.url)
                    }
                }
            }
            TButton {
                id: eject
                visible: row.ejectable && row.mounted
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                height: 22
                symbol: "eject"
                onClicked: App.places.eject(row.udi)
            }
            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["text/uri-list"]
                enabled: row.kind !== "tag" && row.mounted && !["recents", "applications"].includes(String(row.url).split(":")[0])
                onDropped: d => {
                    App.ops.drop(d.urls, row.url, App.ops.dropWantsCopy(DragState.active, d.proposedAction))
                    d.acceptProposedAction()
                }
            }
            FMenu {
                id: menu
                FMenuItem {
                    text: qsTr("Mount")
                    visible: row.kind === "volume" && !row.mounted
                    onTriggered: App.places.mount(row.udi)
                }
                FMenuItem {
                    text: qsTr("Open in New Window")
                    enabled: row.mounted
                    onTriggered: App.requestWindow(row.url)
                }
                FMenuItem {
                    text: qsTr("Remove from Sidebar")
                    visible: App.places.isFavorite(row.url)
                    onTriggered: App.places.removeFavorite(row.url)
                }
                FMenuItem {
                    text: qsTr("Eject")
                    visible: row.ejectable && row.mounted
                    onTriggered: App.places.eject(row.udi)
                }
                FMenuItem {
                    text: qsTr("Empty Trash")
                    visible: String(row.url) === "trash:/"
                    enabled: !App.trashEmpty
                    onTriggered: side.win.confirmEmptyTrash()
                }
            }
        }
    }
    Connections {
        target: App.places
        function onMounted(url) {
            if (side.win.active)
                side.win.go(url)
        }
        function onEjected(url) {
            if (App.isInside(side.win.location, url))
                side.win.go(App.homeUrl)
        }
    }
}
