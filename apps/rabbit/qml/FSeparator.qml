import QtQuick
import QtQuick.Controls.Basic

MenuSeparator {
    implicitHeight: visible ? 9 : 0
    padding: 0
    contentItem: Item {
        Rectangle {
            x: 8
            width: parent.width - 16
            height: 1
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.border
        }
    }
}
