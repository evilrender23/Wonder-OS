import QtQuick
import "Bindings.js" as Bindings

/// A key combo drawn as key caps.
Row {
    id: root

    property string combo: ""
    property string modKey: "super"
    property bool off: false

    spacing: 4
    opacity: off ? 0.4 : 1

    Repeater {
        model: Bindings.caps(root.combo, root.modKey)

        Rectangle {
            required property string modelData

            width: Math.max(24, cap.implicitWidth + 12)
            height: 24
            radius: 6
            color: Theme.cap
            border.width: 1
            border.color: Theme.border

            Text {
                id: cap

                anchors.centerIn: parent
                text: parent.modelData
                color: Theme.text
                font.pixelSize: Theme.small
                font.strikeout: root.off
            }
        }
    }
}
