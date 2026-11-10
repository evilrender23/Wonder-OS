import QtQuick
import Quickshell
import ".."
import "../services"
import "../components"

// One section of the bar. Unlocked (Settings.data.layoutLocked = false), each
// widget can be dragged by its grip to any position in any section, or hidden
// with its ×; the layout is saved on drop.
Item {
    id: root

    required property string section
    required property var bar
    readonly property bool unlocked: !Settings.data.layoutLocked
    readonly property var ids: bar.widgetsFor(section)
    // Where a drag would land, for the insertion marker (-1: not over us).
    property int dropIndex: -1

    implicitWidth: Math.max(row.implicitWidth, unlocked ? 40 : 0)
    implicitHeight: Theme.pillHeight
    width: implicitWidth
    height: implicitHeight

    function indexAt(x) {
        for (let i = 0; i < row.children.length; i++) {
            const c = row.children[i]
            if (c.widgetId === undefined) continue   // the Repeater itself
            if (x < c.x + c.width / 2) return c.index
        }
        return ids.length
    }

    function move(id, fromSection, toIndex) {
        const layout = JSON.parse(JSON.stringify(Settings.data.layout || Settings.defaultLayout))
        for (const s of ["left", "center", "right"]) layout[s] = layout[s] || []
        const from = layout[fromSection].indexOf(id)
        if (from >= 0) {
            layout[fromSection].splice(from, 1)
            if (fromSection === section && from < toIndex) toIndex--
        }
        layout[section].splice(Math.max(0, Math.min(toIndex, layout[section].length)), 0, id)
        Settings.setLayout(layout)
    }

    Rectangle {
        // Drop zone outline while editing, so empty sections stay reachable.
        anchors.fill: parent
        anchors.margins: -3
        visible: root.unlocked
        radius: Theme.radius + 2
        color: drop.containsDrag ? Theme.p.accentSoft : "transparent"
        border.width: 1
        border.color: drop.containsDrag ? Theme.p.accent : Theme.p.stroke2
    }

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.unlocked ? 6 : 1
        move: Transition { NumberAnimation { properties: "x"; duration: Theme.animMs; easing.type: Easing.OutCubic } }

        Repeater {
            model: root.ids
            delegate: WidgetHost {
                required property string modelData
                required property int index
                widgetId: modelData
                section: root.section
                bar: root.bar
                showMarker: root.dropIndex === index
            }
        }
    }

    Rectangle {
        // Insertion marker after the last widget.
        visible: root.dropIndex === root.ids.length && drop.containsDrag
        x: row.x + row.width + 1
        anchors.verticalCenter: parent.verticalCenter
        width: 2; height: Theme.pillHeight
        radius: 1
        color: Theme.p.accent
    }

    DropArea {
        id: drop
        anchors.fill: parent
        anchors.margins: -6
        keys: ["wonderland-bar-widget"]
        onPositionChanged: drag => root.dropIndex = root.indexAt(drag.x - 6)
        onExited: root.dropIndex = -1
        onDropped: drop => {
            const [id, from] = drop.text.split("|")
            root.move(id, from, root.indexAt(drop.x - 6))
            root.dropIndex = -1
            drop.accept()
        }
    }
}
