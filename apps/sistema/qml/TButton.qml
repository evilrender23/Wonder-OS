import QtQuick
import QtQuick.Controls

Button {
    id: root

    /// The one action a dialog or page is there for.
    property bool primary: false
    property bool destructive: false
    property string tip: ""

    implicitHeight: 30
    leftPadding: 12
    rightPadding: 12
    font.pixelSize: Theme.body
    opacity: enabled ? 1 : 0.4

    ToolTip.visible: tip !== "" && hovered
    ToolTip.delay: 600
    ToolTip.text: tip

    contentItem: Text {
        text: root.text
        font: root.font
        color: root.primary ? "#ffffff" : (root.destructive ? Theme.danger : Theme.text)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 7
        color: root.primary ? Theme.accent : (root.down ? Theme.selection : (root.hovered ? Theme.hover : Theme.field))
        opacity: root.primary && root.down ? 0.8 : 1
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
}
