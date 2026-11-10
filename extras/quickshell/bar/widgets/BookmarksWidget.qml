import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"
import "../../panels"

// Canvas bookmarks in place of workspaces. The active one is the bookmark the
// compositor reports nearest the view (what ext-workspace marks active).
// Styles (Settings.data.bookmarkStyle): default (dots), numbers, magic (a
// sliding accent blob). Left click jumps, scroll steps, right click opens the
// canvas panel.
Item {
    id: root

    property var bar
    property string widgetId: "bookmarks"
    readonly property string style: Settings.data.bookmarkStyle
    readonly property var names: Compositor.bookmarkNames
    readonly property int activeIndex: names.indexOf(Compositor.activeBookmark)

    implicitWidth: Math.max(cells.implicitWidth + 16, 40)
    implicitHeight: Theme.pillHeight

    function occupied(name) {
        const b = Compositor.bookmarks[name]
        if (!b) return false
        return Compositor.windows.some(w => Math.abs(w.position[0] - b[0]) < 1400 && Math.abs(w.position[1] - b[1]) < 900)
    }

    function step(dir) {
        if (names.length === 0) return
        const i = activeIndex < 0 ? (dir > 0 ? 0 : names.length - 1) : (activeIndex + dir + names.length) % names.length
        Compositor.goToBookmark(names[i])
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: mouse.containsMouse || panel.visible ? Theme.p.hover : "transparent"
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.RightButton
        onClicked: panel.toggle()
        onWheel: event => root.step(event.angleDelta.y > 0 ? -1 : 1)
    }

    // Magic: one accent blob that slides and stretches to the active cell.
    Rectangle {
        id: blob
        visible: root.style === "magic" && root.activeIndex >= 0
        readonly property Item cell: root.activeIndex >= 0 ? repeater.itemAt(root.activeIndex) : null
        x: cell ? cells.x + cell.x - 3 : 0
        width: cell ? cell.width + 6 : 0
        height: 20
        anchors.verticalCenter: parent.verticalCenter
        radius: 10
        color: Theme.p.accent
        Behavior on x { SpringAnimation { spring: 4; damping: 0.32; epsilon: 0.2 } }
        Behavior on width { SpringAnimation { spring: 5; damping: 0.4; epsilon: 0.2 } }
    }

    Row {
        id: cells
        anchors.centerIn: parent
        spacing: root.style === "default" ? 6 : 4

        Repeater {
            id: repeater
            model: root.names

            delegate: Item {
                id: cell
                required property string modelData
                required property int index
                readonly property bool current: index === root.activeIndex
                readonly property bool busy: root.occupied(modelData)
                readonly property string label: root.style === "numbers" || root.style === "magic"
                    ? (isNaN(Number(modelData)) ? modelData : String(index + 1)) : ""

                implicitWidth: root.style === "default" ? (current ? 20 : 8) : Math.max(20, labelText.implicitWidth + 12)
                implicitHeight: 20
                Behavior on implicitWidth { NumberAnimation { duration: Theme.animMs; easing.type: Easing.OutCubic } }

                Rectangle {
                    visible: root.style === "default"
                    anchors.centerIn: parent
                    width: parent.implicitWidth
                    height: 8
                    radius: 4
                    color: cell.current ? Theme.p.accent : cell.busy ? Theme.p.text3 : Theme.p.stroke1
                    Behavior on color { ColorAnimation { duration: Theme.animMs } }
                }

                Rectangle {
                    visible: root.style === "numbers"
                    anchors.fill: parent
                    radius: Theme.radius
                    color: cell.current ? Theme.p.accent : cellMouse.containsMouse ? Theme.p.active : "transparent"
                    border.width: cell.busy && !cell.current ? 1 : 0
                    border.color: Theme.p.stroke1
                }

                Text {
                    id: labelText
                    visible: root.style !== "default"
                    anchors.centerIn: parent
                    text: cell.label
                    color: cell.current ? Theme.p.onAccent : cell.busy ? Theme.p.text : Theme.p.text3
                    font.pixelSize: Theme.smallFontSize + 1
                    font.family: Theme.fontFamily
                    font.weight: cell.current ? Font.DemiBold : Font.Normal
                }

                MouseArea {
                    id: cellMouse
                    anchors.fill: parent
                    anchors.margins: -2
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Compositor.goToBookmark(cell.modelData)
                }

                Tooltip { target: cell; text: cell.modelData; shown: cellMouse.containsMouse }
            }
        }
    }

    Label {
        visible: root.names.length === 0
        anchors.centerIn: parent
        role: "muted"
        text: Compositor.connected ? "no bookmarks" : "offline"
    }

    CanvasPanel { id: panel; target: root }

    Connections {
        target: Popups
        function onPanelRequested(id) { if (id === root.widgetId && root.QsWindow.window.screen === Quickshell.screens[0]) panel.toggleFromIpc() }
    }
}
