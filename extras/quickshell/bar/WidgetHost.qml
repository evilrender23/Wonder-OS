import QtQuick
import Quickshell
import ".."
import "../services"
import "../components"

// Loads one bar widget by id and, while the layout is unlocked, adds the
// grip that drags it and the × that hides it.
Item {
    id: root

    required property string widgetId
    required property string section
    required property var bar
    property bool showMarker: false
    readonly property bool unlocked: !Settings.data.layoutLocked
    readonly property bool shown: unlocked || !loader.item || loader.item.shown !== false

    visible: shown
    implicitWidth: shown ? (loader.item ? loader.item.implicitWidth : 0) + (unlocked ? grip.width + 2 : 0) : 0
    implicitHeight: Theme.pillHeight
    width: implicitWidth
    height: implicitHeight
    opacity: dragArea.drag.active ? 0.35 : 1

    Rectangle {
        visible: root.showMarker
        x: -4; width: 2; height: parent.height
        radius: 1
        color: Theme.p.accent
    }

    Loader {
        id: loader
        x: root.unlocked ? grip.width + 2 : 0
        anchors.verticalCenter: parent.verticalCenter
        source: root.bar.registry[root.widgetId] || ""
        onLoaded: {
            if ("bar" in item) item.bar = root.bar
            if ("widgetId" in item) item.widgetId = root.widgetId
        }
        // Clicks are for dragging while editing.
        enabled: !root.unlocked
    }

    Rectangle {
        id: grip
        visible: root.unlocked
        width: 14
        height: parent.height
        radius: Theme.radius
        color: dragArea.containsMouse ? Theme.p.active : Theme.p.hover
        Icon { anchors.centerIn: parent; name: "grip-vertical"; size: 12; color: Theme.p.text3 }
    }

    Rectangle {
        visible: root.unlocked
        anchors { top: parent.top; right: parent.right; topMargin: -5; rightMargin: -5 }
        width: 14; height: 14; radius: 7
        color: hideMouse.containsMouse ? Theme.p.red : Theme.p.elevated
        border.width: 1
        border.color: Theme.p.stroke1
        Icon { anchors.centerIn: parent; name: "x"; size: 9; color: hideMouse.containsMouse ? "white" : Theme.p.text2 }
        MouseArea {
            id: hideMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                const layout = JSON.parse(JSON.stringify(Settings.data.layout))
                layout[root.section] = layout[root.section].filter(id => id !== root.widgetId)
                Settings.setLayout(layout)
            }
        }
    }

    // The drag carries a ghost so the Row keeps laying out the real item.
    MouseArea {
        id: dragArea
        anchors.fill: parent
        anchors.rightMargin: 10
        enabled: root.unlocked
        hoverEnabled: true
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        drag.target: ghost
        onPressed: event => {
            const p = root.mapToItem(root.bar.contentItem, 0, 0)
            ghost.parent = root.bar.contentItem
            ghost.x = p.x
            ghost.y = p.y
            ghost.Drag.hotSpot.x = event.x
            ghost.Drag.hotSpot.y = event.y
        }
        onReleased: {
            if (drag.active) ghost.Drag.drop()
            ghost.parent = root
            ghost.x = 0
            ghost.y = 0
        }
    }

    Rectangle {
        id: ghost
        width: root.width
        height: root.height
        radius: Theme.radius
        visible: dragArea.drag.active
        color: Theme.p.elevated
        border.width: 1
        border.color: Theme.p.accent
        z: 100
        Drag.active: dragArea.drag.active
        Drag.keys: ["wonderland-bar-widget"]
        Drag.mimeData: ({ "text/plain": `${root.widgetId}|${root.section}` })
        Drag.supportedActions: Qt.MoveAction
        Drag.dragType: Drag.Internal

        Text {
            anchors.centerIn: parent
            text: root.widgetId
            color: Theme.p.accent
            font.pixelSize: Theme.smallFontSize
            font.family: Theme.fontFamily
        }
    }
}
