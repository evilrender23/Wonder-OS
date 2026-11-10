import QtQuick
import QtQuick.Layouts
import Quickshell
import ".."
import "../services"

// The drop-down panel a bar widget opens: an xdg popup under `target` with a
// Hermes card (surface, hairline border, soft shadow), a title row and a
// column for content. One panel is open at a time (Popups.current).
// A titled panel carries a close button, and content taller than the screen
// scrolls with the wheel instead of running off the bottom edge.
PopupWindow {
    id: root

    required property Item target
    property string title: ""
    property string icon: ""
    property int contentWidth: 300
    property int padding: 14
    property int contentSpacing: 8
    // A menu (MenuPanel) hangs from its title's left edge, close to the bar,
    // and hovering another menu title of the bar switches to that menu.
    property bool menu: false
    default property alias content: inner.data
    property alias headerExtra: headerRow.data

    anchor.item: target
    anchor.edges: menu ? Edges.Bottom | Edges.Left : Edges.Bottom
    anchor.gravity: menu ? Edges.Bottom | Edges.Right : Edges.Bottom
    anchor.margins.top: menu ? 4 - shadowPad : 8
    anchor.margins.left: menu ? -shadowPad : 0
    // A grabbing xdg_popup needs a fresh input serial, which an IPC/keybind
    // open doesn't have; those open ungrabbed, so a click elsewhere can't
    // dismiss them: the close button, the bar or the shortcut again does.
    property bool fromIpc: false
    grabFocus: !fromIpc
    color: "transparent"
    implicitWidth: contentWidth + 2 * padding + 2 * shadowPad
    implicitHeight: column.implicitHeight + 2 * padding + 2 * shadowPad

    readonly property int shadowPad: 10
    // What's left for the content between the bar and the screen's bottom.
    readonly property real screenHeight: target && target.QsWindow.window && target.QsWindow.window.screen
        ? target.QsWindow.window.screen.height : 100000
    readonly property real maxContentHeight: Math.max(120, screenHeight - Theme.barHeight - 16
        - 2 * padding - 2 * shadowPad - (header.visible ? header.implicitHeight + column.spacing : 0))

    function toggle() { visible = !visible }
    function open() { visible = true }
    function close() { visible = false }
    function toggleFromIpc() {
        if (visible) { visible = false; return }
        fromIpc = true
        visible = true
    }

    onVisibleChanged: {
        if (visible) {
            if (Popups.current && Popups.current !== root) Popups.current.visible = false
            Popups.current = root
        } else {
            fromIpc = false
            if (Popups.current === root) Popups.current = null
        }
    }

    // Two stacked translucent rects approximate Hermes' layered --shadow-nous.
    Rectangle {
        x: root.shadowPad - 2; y: root.shadowPad + 2
        width: card.width + 4; height: card.height + 4
        radius: Theme.popupRadius + 2
        color: Theme.p.shadow
        opacity: Theme.dark ? 0.35 : 0.06
    }

    Rectangle {
        id: card
        x: root.shadowPad; y: root.shadowPad
        width: parent.width - 2 * root.shadowPad
        height: parent.height - 2 * root.shadowPad
        radius: Theme.popupRadius
        color: Theme.p.elevated
        border.color: Theme.p.stroke2
        border.width: 1

        ColumnLayout {
            id: column
            x: root.padding; y: root.padding
            width: root.contentWidth
            spacing: 10

            RowLayout {
                id: header
                visible: root.title !== ""
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    id: headerRow
                    Layout.fillWidth: true
                    spacing: 8

                    Icon { visible: root.icon !== ""; name: root.icon; color: Theme.p.accent; size: 16 }
                    Text {
                        Layout.fillWidth: true
                        text: root.title
                        color: Theme.p.text
                        font.pixelSize: Theme.titleFontSize
                        font.family: Theme.fontFamily
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
                IconButton { icon: "x"; size: 24; tooltip: "Cerrar"; onClicked: root.close() }
            }

            Flickable {
                id: scroller
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(inner.implicitHeight, root.maxContentHeight)
                contentHeight: inner.implicitHeight
                readonly property bool overflows: contentHeight > height + 1
                clip: overflows
                // Wheel only: a drag-to-flick would fight the sliders inside.
                interactive: false
                onOverflowsChanged: if (!overflows) contentY = 0

                ColumnLayout {
                    id: inner
                    width: scroller.width
                    spacing: root.contentSpacing
                }

                WheelHandler {
                    enabled: scroller.overflows
                    onWheel: event => scroller.contentY = Math.max(0, Math.min(
                        scroller.contentHeight - scroller.height, scroller.contentY - event.angleDelta.y))
                }

                Rectangle {
                    visible: scroller.overflows
                    parent: scroller
                    anchors.right: parent.right
                    width: 3
                    radius: 1.5
                    color: Theme.veil(0.3)
                    height: Math.max(24, scroller.height * scroller.height / scroller.contentHeight)
                    y: (scroller.height - height) * scroller.contentY / Math.max(1, scroller.contentHeight - scroller.height)
                }
            }
        }
    }
}
