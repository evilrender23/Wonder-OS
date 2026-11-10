import QtQuick
import QtQuick.Effects

// A user's picture in a circle, or their initial when they have none.
Item {
    id: root

    property string name
    property string image
    property int size: 96

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        id: disc
        anchors.fill: parent
        radius: width / 2
        color: Style.veil(0.10)
        border.width: 1
        border.color: Style.veil(0.22)
    }

    Text {
        anchors.centerIn: parent
        visible: picture.status !== Image.Ready
        text: root.name ? root.name.charAt(0).toUpperCase() : "?"
        color: Style.text
        font.family: Style.fontFamily
        font.pixelSize: Math.round(root.size * 0.40)
        font.weight: Font.Light
    }

    Image {
        id: picture
        anchors.fill: parent
        anchors.margins: 1
        source: root.image ? "file://" + root.image : ""
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectCrop
        visible: false
    }

    Rectangle {
        id: mask
        anchors.fill: picture
        radius: width / 2
        layer.enabled: true
        visible: false
    }

    MultiEffect {
        anchors.fill: picture
        source: picture
        visible: picture.status === Image.Ready
        maskEnabled: true
        maskSource: mask
    }
}
