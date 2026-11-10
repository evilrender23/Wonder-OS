import QtQuick
import QtQuick.Effects
import ".."
import "../services"

// A Lucide line icon from assets/icons, tinted to `color`. The SVGs are drawn
// white so a full colorization reproduces the tint exactly.
Item {
    id: root

    property string name
    property color color: Theme.p.text
    property int size: Theme.iconSize

    implicitWidth: size
    implicitHeight: size

    Image {
        id: img
        anchors.fill: parent
        source: root.name ? Qt.resolvedUrl(`../assets/icons/${root.name}.svg`) : ""
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        smooth: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: img
        colorization: 1.0
        colorizationColor: root.color
        Behavior on colorizationColor { ColorAnimation { duration: Theme.animMs } }
    }
}
