import QtQuick
import ".."
import "../services"

// Single-line text input in the Hermes field style.
Rectangle {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property bool password: false
    signal accepted()

    function focusInput() { input.forceActiveFocus() }

    implicitHeight: 30
    implicitWidth: 180
    radius: Theme.radius
    color: Theme.p.surface
    border.width: 1
    border.color: input.activeFocus ? Theme.p.accent : Theme.p.stroke2

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 9
        anchors.rightMargin: 9
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.p.text
        selectionColor: Theme.p.accentSoft
        selectedTextColor: Theme.p.text
        font.pixelSize: Theme.fontSize
        font.family: Theme.fontFamily
        echoMode: root.password ? TextInput.Password : TextInput.Normal
        clip: true
        onAccepted: root.accepted()

        Text {
            visible: !input.text && !input.activeFocus
            anchors.verticalCenter: parent.verticalCenter
            text: root.placeholder
            color: Theme.p.text4
            font: input.font
        }
    }
}
