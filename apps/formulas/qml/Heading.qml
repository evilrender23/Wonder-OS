import QtQuick

/// The name of a group of lines in a panel.
Item {
    property alias text: label.text
    width: parent ? parent.width : 0
    height: 26
    Text {
        id: label
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 5
        color: Theme.text
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
}
