import QtQuick
import QtQuick.Controls.Basic

/// A line of text to type in.
TextField {
    id: field
    /// What was typed makes no sense: say so with the border.
    property bool wrong: false
    color: Theme.text
    font.pixelSize: 13
    placeholderTextColor: Theme.faint
    selectionColor: Theme.accent
    selectedTextColor: "#ffffff"
    selectByMouse: true
    leftPadding: 9
    rightPadding: 9
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    implicitHeight: 28
    background: Rectangle {
        radius: 7
        color: Theme.field
        border.color: field.wrong ? Theme.tint : (field.activeFocus ? Theme.accent : "transparent")
    }
}
