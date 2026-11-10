import QtQuick
import QtQuick.Controls

/// A switch that shows what is, not what was asked: `on` stays bound to the real state
/// and `asked` carries the wish.
AbstractButton {
    id: root

    property bool on: false
    signal asked(bool value)

    implicitWidth: 42
    implicitHeight: 24
    opacity: enabled ? 1 : 0.4
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: on
    onClicked: asked(!on)
    Keys.onSpacePressed: asked(!on)

    background: Rectangle {
        radius: height / 2
        color: root.on ? Theme.accent : Theme.track
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.text
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            width: 20
            height: 20
            radius: 10
            y: 2
            x: root.on ? parent.width - width - 2 : 2
            color: "#ffffff"
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
}
