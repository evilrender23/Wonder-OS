import QtQuick
import QtQuick.Controls

TextField {
    id: root

    implicitHeight: 30
    leftPadding: 9
    rightPadding: 9
    font.pixelSize: Theme.body
    color: Theme.text
    placeholderTextColor: Theme.faint
    selectionColor: Theme.accent
    selectedTextColor: "#ffffff"
    selectByMouse: true

    background: Rectangle {
        radius: 7
        color: Theme.field
        border.width: root.activeFocus ? 2 : 0
        border.color: Theme.accent
    }
}
