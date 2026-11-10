import QtQuick
import QtQuick.Effects

// A Lucide line icon from assets/icons, tinted to `color` (the SVGs are white).
Item {
    id: root

    property string name
    property color color: Style.text
    property int size: Style.iconSize

    implicitWidth: size
    implicitHeight: size

    Image {
        id: img
        anchors.fill: parent
        source: root.name ? Qt.resolvedUrl(`assets/icons/${root.name}.svg`) : ""
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        smooth: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: img
        colorization: 1.0
        colorizationColor: root.color
    }
}
