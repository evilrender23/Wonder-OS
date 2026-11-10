import QtQuick
import QtQuick.Layouts
import Quickshell
import ".."
import "../services"

// One title of the focused app's own menu bar, with its drop-down. `owner` is
// the appmenu widget: it knows which menu of the bar is open.
MenuTitle {
    id: root

    required property var owner
    required property var entry          // { id, label } from AppMenu.items
    readonly property string key: `app:${entry.id}`

    text: entry.label
    open: owner.current === key
    onPressed: panel.visible = !panel.visible
    onEntered: if (owner.current !== "" && owner.current !== key) panel.visible = true

    function show(fromIpc) {
        panel.fromIpc = fromIpc
        panel.visible = true
    }

    MenuPanel {
        id: panel
        target: root
        contentWidth: Math.max(220, Math.min(520, rows.implicitWidth))
        onVisibleChanged: {
            if (visible) { root.owner.current = root.key; AppMenu.open(root.entry.id) }
            else if (root.owner.current === root.key) { root.owner.current = ""; AppMenu.close() }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(rows.implicitHeight, (panel.screen ? panel.screen.height : 900) - 80)
            contentHeight: rows.implicitHeight
            clip: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: rows
                width: parent.width
                spacing: 0

                AppMenuItems {
                    Layout.fillWidth: true
                    items: panel.visible ? AppMenu.childrenOf(root.entry) : []
                    onActivated: id => { panel.close(); AppMenu.activate(id) }
                }
            }
        }
    }
}
