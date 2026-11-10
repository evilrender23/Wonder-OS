import QtQuick
import Quickshell
import QtQuick.Layouts
import ".."
import "../services"

// A bar widget: optional icon + text, flat like a macOS menu-bar item — no
// box at rest, a soft rounded highlight on hover and while its menu is open.
// Per-widget style (fill, border, compact) comes from Settings so the
// customize panel can change one widget without touching the others.
Item {
    id: root

    property string widgetId: ""
    property string icon: ""
    property string text: ""
    property color iconColor: Theme.p.text
    property color textColor: Theme.p.text
    property bool active: false              // e.g. its panel is open
    property bool urgent: false
    // False hides the widget (no backlight, no battery…) without unloading it.
    property bool shown: true
    property string tooltip: ""
    // The widget's BarPanel, if any, so IPC can toggle it.
    property var panel: null
    default property alias extra: row.data
    readonly property var style: Settings.widgetStyle(widgetId)
    readonly property bool compact: style.compact === true
    readonly property bool hovered: mouse.containsMouse

    signal clicked()
    signal rightClicked()
    signal middleClicked()
    signal scrolled(int delta)

    implicitHeight: Theme.pillHeight
    implicitWidth: row.implicitWidth + 2 * (compact ? 6 : 8)

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.active ? Theme.veil(0.16)
            : root.hovered ? Theme.veil(0.08)
            : root.style.fill === "solid" ? Theme.p.surface
            : root.style.fill === "tint" ? Theme.p.accentSoft
            : "transparent"
        border.width: root.style.border === true ? 1 : 0
        border.color: Theme.p.stroke2
        Behavior on color { ColorAnimation { duration: Theme.animMs } }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Icon {
            visible: root.icon !== ""
            name: root.icon
            color: root.urgent ? Theme.p.red : root.iconColor
        }

        Text {
            visible: root.text !== "" && !(root.compact && root.icon !== "")
            text: root.text
            color: root.urgent ? Theme.p.red : root.textColor
            font.pixelSize: Theme.fontSize
            font.family: Theme.fontFamily
            font.weight: Font.Medium
            font.features: { "tnum": 1 }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        cursorShape: Qt.PointingHandCursor
        onClicked: event => {
            if (event.button === Qt.RightButton) root.rightClicked()
            else if (event.button === Qt.MiddleButton) root.middleClicked()
            else root.clicked()
        }
        onWheel: event => root.scrolled(event.angleDelta.y)
    }

    Tooltip {
        target: root
        text: root.tooltip
        shown: root.hovered && root.tooltip !== "" && !root.active
    }

    Connections {
        target: Popups
        function onPanelRequested(id) {
            if (id === root.widgetId && root.panel && root.QsWindow.window
                    && root.QsWindow.window.screen === Quickshell.screens[0])
                root.panel.toggleFromIpc()
        }
    }
}
