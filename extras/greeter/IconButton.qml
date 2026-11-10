import QtQuick

// A flat round button: an icon, a hover veil and a label that fades in above.
Item {
    id: root

    property string icon
    property string label
    property int size: 34
    signal clicked()

    implicitWidth: size
    implicitHeight: size
    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Style.veil(area.pressed ? 0.20 : area.containsMouse || root.activeFocus ? 0.12 : 0.0)
        border.width: 1
        border.color: Style.veil(root.activeFocus ? 0.35 : 0.0)
        Behavior on color { ColorAnimation { duration: Style.animMs } }
    }

    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: 17
        color: Style.text
        opacity: area.containsMouse || root.activeFocus ? 1.0 : 0.72
        Behavior on opacity { NumberAnimation { duration: Style.animMs } }
    }

    Text {
        anchors.bottom: parent.top
        anchors.bottomMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: Style.text2
        font.family: Style.fontFamily
        font.pixelSize: Style.smallFontSize
        opacity: area.containsMouse || root.activeFocus ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: Style.animMs } }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
