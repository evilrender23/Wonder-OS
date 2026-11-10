import QtQuick

/// A thin line between two groups of the bar.
Item {
    width: 13
    height: 26
    Rectangle {
        anchors.centerIn: parent
        width: 1
        height: 16
        color: Theme.border
    }
}
