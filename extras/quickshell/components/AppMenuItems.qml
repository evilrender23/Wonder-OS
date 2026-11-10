import QtQuick
import QtQuick.Layouts
import ".."
import "../services"

// The rows of one level of an app's own menu (services/AppMenu.qml). A
// submenu is a row that unfolds its entries in place, indented, instead of a
// second popup: it works the same with the mouse far from the panel's edge.
ColumnLayout {
    id: root

    property var items: []
    property int depth: 0
    signal activated(int id)

    readonly property var shown: items.filter(i => i.visible !== false)
    // Ticks need their column only in menus that have something to tick.
    readonly property bool ticks: shown.some(i => i.toggle)

    spacing: 0

    Repeater {
        model: root.shown
        delegate: ColumnLayout {
            id: entry
            required property var modelData
            property bool expanded: false
            readonly property bool isMenu: modelData.submenu === true

            Layout.fillWidth: true
            spacing: 0

            MenuSeparator { visible: entry.modelData.separator === true }
            MenuRow {
                visible: entry.modelData.separator !== true
                Layout.leftMargin: root.depth * 14
                text: (root.ticks ? (entry.modelData.checked ? "✓  " : "     ") : "") + (entry.modelData.label || "")
                shortcut: entry.isMenu ? (entry.expanded ? "⌄" : "›") : (entry.modelData.shortcut || "")
                enabled: entry.modelData.enabled !== false
                onTriggered: {
                    if (!entry.isMenu) { root.activated(entry.modelData.id); return }
                    entry.expanded = !entry.expanded
                    if (entry.expanded) AppMenu.open(entry.modelData.id)
                }
            }
            Loader {
                Layout.fillWidth: true
                active: entry.isMenu && entry.expanded
                visible: active
                source: "AppMenuItems.qml"
                onLoaded: {
                    item.depth = root.depth + 1
                    item.items = Qt.binding(() => AppMenu.childrenOf(entry.modelData))
                    item.activated.connect(root.activated)
                }
            }
        }
    }
}
