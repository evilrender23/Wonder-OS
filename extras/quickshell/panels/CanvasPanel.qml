import QtQuick
import QtQuick.Layouts
import Quickshell
import ".."
import "../services"
import "../components"

// Bookmarks, the windows on the canvas (live and suspended), the bookmark
// style and the zoom: the canvas' answer to a workspace panel.
BarPanel {
    id: root

    title: "Canvas"
    icon: "map-pin"
    contentWidth: 330

    headerExtra: [
        Label { role: "caption"; text: `${Math.round(Compositor.zoom * 100)}%` },
        IconButton { icon: "zoom-out"; size: 24; tooltip: "Zoom out"; onClicked: Compositor.action("zoom-out") },
        IconButton { icon: "zoom-in"; size: 24; tooltip: "Zoom in"; onClicked: Compositor.action("zoom-in") },
        IconButton { icon: "scan"; size: 24; tooltip: "Overview"; onClicked: Compositor.action("zoom-to-fit") },
        IconButton { icon: "house"; size: 24; tooltip: "Home"; onClicked: Compositor.action("home-toggle") }
    ]

    Label { role: "section"; text: "Bookmarks" }

    Repeater {
        model: Compositor.bookmarkNames
        delegate: ListItem {
            required property string modelData
            icon: "bookmark"
            text: modelData
            detail: Compositor.bookmarks[modelData] ? `${Math.round(Compositor.bookmarks[modelData][0])}, ${Math.round(Compositor.bookmarks[modelData][1])}` : ""
            highlighted: Compositor.activeBookmark === modelData
            actionIcon: "trash-2"
            actionTooltip: "Delete"
            onClicked: { Compositor.goToBookmark(modelData); root.close() }
            onActionClicked: Compositor.deleteBookmark(modelData)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        TextBox {
            id: newName
            Layout.fillWidth: true
            placeholder: "Save this view as…"
            onAccepted: save.clicked()
        }
        IconButton {
            id: save
            icon: "plus"
            primary: true
            enabled: newName.text.trim() !== ""
            onClicked: {
                Compositor.setBookmark(newName.text.trim().replace(/\s+/g, "-"))
                newName.text = ""
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Label { role: "caption"; text: "Style"; Layout.fillWidth: true }
        Repeater {
            model: ["default", "numbers", "magic"]
            delegate: IconButton {
                required property string modelData
                text: modelData
                size: 24
                checked: Settings.data.bookmarkStyle === modelData
                onClicked: Settings.data.bookmarkStyle = modelData
            }
        }
    }

    Divider {}

    Label { role: "section"; text: `Windows · ${Compositor.windows.length}` }

    Flickable {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 260)
        contentHeight: list.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: list
            width: parent.width
            spacing: 2

            Repeater {
                model: Compositor.windows
                delegate: ListItem {
                    required property var modelData
                    icon: modelData.suspended ? "moon" : "app-window"
                    iconColor: modelData.suspended ? Theme.p.text4 : Theme.p.text2
                    text: modelData.title || modelData.app_id || "untitled"
                    detail: modelData.suspended ? `${modelData.app_id} · suspended` : modelData.app_id
                    highlighted: modelData.is_focused
                    actionIcon: "x"
                    actionTooltip: modelData.suspended ? "Dismiss" : "Close"
                    onClicked: {
                        if (modelData.suspended) Compositor.relaunch(modelData.id)
                        else Compositor.focusWindow(modelData.id)
                        root.close()
                    }
                    onActionClicked: Compositor.closeWindow(modelData.id)
                }
            }
        }
    }
}
