import QtQuick
import QtQuick.Layouts
import Quickshell
import "../components"
import ".."

// "Acerca de este equipo" in the ☤ menu: the project's credits, in a window of
// its own (a real toplevel, so it moves and closes like any other window).
// `qs ipc call bar togglePanel about` opens it too.
Scope {
    id: root

    property bool open: false

    Connections {
        target: Popups
        function onAboutRequested() { root.open = true }
        function onPanelRequested(id) { if (id === "about") root.open = !root.open }
    }

    LazyLoader {
        active: root.open

        FloatingWindow {
            id: win

            title: "Acerca de este equipo"
            color: Theme.p.chrome
            implicitWidth: 380
            implicitHeight: 260
            minimumSize: Qt.size(380, 260)
            maximumSize: Qt.size(380, 260)
            visible: true
            onVisibleChanged: if (!visible) root.open = false

            Item {
                anchors.fill: parent
                focus: true
                Keys.onEscapePressed: root.open = false

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 6

                    Text {
                        text: "☤"
                        color: Theme.p.accent
                        font.pixelSize: 72
                        font.family: Theme.fontFamily
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "WonderlandWM"
                        role: "title"
                        font.pixelSize: 20
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 10
                    }
                    Label {
                        text: "(c) 2026 - Alister M."
                        textFormat: Text.PlainText
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "Proyecto crafteado con <3"
                        textFormat: Text.PlainText
                        role: "muted"
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }
    }
}
