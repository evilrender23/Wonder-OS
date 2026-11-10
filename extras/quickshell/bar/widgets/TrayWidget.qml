import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Widgets
import Quickshell.Services.SystemTray
import "../.."
import "../../services"
import "../../components"

// StatusNotifier items. Left click activates, right click opens the item's
// menu, middle click hides it (Settings.data.hiddenTrayItems); the chevron
// shows hidden items and lets you bring them back.
Item {
    id: root

    property var bar
    property string widgetId: "tray"
    property bool expanded: false
    readonly property var items: SystemTray.items.values
    readonly property var visibleItems: items.filter(i => expanded || !Settings.data.hiddenTrayItems.includes(i.id))
    readonly property bool hasHidden: items.some(i => Settings.data.hiddenTrayItems.includes(i.id))
    readonly property bool shown: items.length > 0

    implicitWidth: row.implicitWidth + 8
    implicitHeight: Theme.pillHeight

    function toggleHidden(id) {
        const list = Settings.data.hiddenTrayItems.slice()
        const i = list.indexOf(id)
        if (i >= 0) list.splice(i, 1)
        else list.push(id)
        Settings.data.hiddenTrayItems = list
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: root.visibleItems
            delegate: Item {
                id: cell
                required property var modelData
                readonly property bool hiddenItem: Settings.data.hiddenTrayItems.includes(modelData.id)
                implicitWidth: 24
                implicitHeight: 24
                opacity: hiddenItem ? 0.45 : 1

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius
                    color: cellMouse.containsMouse ? Theme.p.hover : "transparent"
                }
                IconImage {
                    anchors.centerIn: parent
                    implicitSize: 16
                    source: cell.modelData.icon
                }
                MouseArea {
                    id: cellMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    onClicked: event => {
                        const item = cell.modelData
                        if (event.button === Qt.MiddleButton) root.toggleHidden(item.id)
                        else if (event.button === Qt.RightButton || item.onlyMenu) {
                            if (item.hasMenu) menu.open()
                        } else item.activate()
                    }
                    onWheel: event => cell.modelData.scroll(event.angleDelta.y, false)
                }
                QsMenuAnchor {
                    id: menu
                    menu: cell.modelData.menu
                    anchor.item: cell
                    anchor.edges: Edges.Bottom
                    anchor.gravity: Edges.Bottom
                }
                Tooltip {
                    target: cell
                    text: cell.modelData.tooltipTitle || cell.modelData.title || cell.modelData.id
                    shown: cellMouse.containsMouse
                }
            }
        }

        IconButton {
            visible: root.hasHidden
            icon: root.expanded ? "chevron-right" : "chevron-left"
            size: 22
            tooltip: root.expanded ? "Collapse" : "Hidden items · middle click an icon to hide it"
            onClicked: root.expanded = !root.expanded
        }
    }
}
