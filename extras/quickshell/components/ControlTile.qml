import QtQuick
import QtQuick.Layouts
import ".."
import "../services"

// A Control Centre module: a rounded translucent card. Children stack in a
// column with the given padding.
Rectangle {
    id: root

    default property alias content: col.data
    property int pad: 10

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + 2 * pad
    radius: 12
    color: Theme.veil(Theme.dark ? 0.07 : 0.05)
    border.width: 1
    border.color: Theme.veil(Theme.dark ? 0.08 : 0.06)

    ColumnLayout {
        id: col
        x: root.pad; y: root.pad
        width: root.width - 2 * root.pad
        spacing: 8
    }
}
