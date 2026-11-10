import QtQuick
import QtQuick.Controls.Basic

/// How this folder's icons are kept: where the user leaves them, on a grid, or sorted.
FMenu {
    id: menu
    required property var win
    required property FolderLayout layout
    title: qsTr("Sort By")
    enabled: win.view === "icons" && win.canArrange

    FMenuItem {
        text: qsTr("None")
        checkable: true
        checked: menu.layout.arrange === ""
        onTriggered: menu.layout.arrange = ""
    }
    FMenuItem {
        text: qsTr("Snap to Grid")
        checkable: true
        checked: menu.layout.arrange === "grid"
        onTriggered: {
            menu.layout.arrange = "grid"
            menu.win.pane.cleanUp()
        }
    }
    FSeparator {}
    Repeater {
        model: [
            { key: "name", title: qsTr("Name") },
            { key: "kind", title: qsTr("Kind") },
            { key: "modified", title: qsTr("Date Modified") },
            { key: "size", title: qsTr("Size") },
        ]
        FMenuItem {
            required property var modelData
            text: modelData.title
            checkable: true
            checked: menu.layout.arrange === modelData.key
            onTriggered: menu.layout.arrange = modelData.key
        }
    }
}
