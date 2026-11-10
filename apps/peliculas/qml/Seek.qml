import QtQuick

/// A thin slider: where the film is, how loud it sounds.
Item {
    id: seek
    /// 0…1.
    property real value: 0
    property bool active: true
    property color fill: Theme.ink
    property color groove: Theme.inkField
    readonly property bool dragging: area.pressed
    readonly property bool hovered: area.containsMouse
    /// Where the pointer is over it, 0…1.
    readonly property real hoverValue: area.at(area.mouseX)
    /// While dragging: where the handle is. `moved` on every step, `released` once at the end.
    property real dragValue: 0
    /// How much one notch of the wheel moves it.
    property real wheelStep: 0.05
    signal moved(real value)
    signal released(real value)
    implicitHeight: 14
    implicitWidth: 100

    readonly property real shown: dragging ? dragValue : Math.max(0, Math.min(1, value))

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: area.containsMouse || area.pressed ? 5 : 3
        radius: height / 2
        color: seek.groove
        Behavior on height { NumberAnimation { duration: 90 } }
        Rectangle {
            width: parent.width * seek.shown
            height: parent.height
            radius: parent.radius
            color: seek.active ? seek.fill : Theme.inkFaint
        }
    }
    Rectangle {
        width: 11
        height: 11
        radius: 6
        x: Math.round((seek.width - width) * seek.shown)
        anchors.verticalCenter: parent.verticalCenter
        color: "#ffffff"
        visible: seek.active && (area.containsMouse || area.pressed)
    }
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        enabled: seek.active
        function at(x) {
            return Math.max(0, Math.min(1, (x - 4) / Math.max(1, seek.width)))
        }
        onPressed: mouse => {
            seek.dragValue = at(mouse.x)
            seek.moved(seek.dragValue)
        }
        onPositionChanged: mouse => {
            if (pressed) {
                seek.dragValue = at(mouse.x)
                seek.moved(seek.dragValue)
            }
        }
        onReleased: seek.released(seek.dragValue)
        onWheel: wheel => {
            const next = Math.max(0, Math.min(1, seek.value + (wheel.angleDelta.y > 0 ? seek.wheelStep : -seek.wheelStep)))
            seek.moved(next)
            seek.released(next)
        }
    }
}
