import QtQuick

/// A number to type, with what it measures after it. Takes a comma or a point.
Item {
    id: control
    property real value: 0
    property int decimals: 1
    property string unit
    property real from: 0
    property real to: 999
    signal edited(real value)
    implicitWidth: 78
    implicitHeight: 28

    function shown() {
        return Number(value).toFixed(decimals).replace(".", ",").replace(/,0+$/, "")
    }

    Field {
        id: field
        anchors.fill: parent
        rightPadding: suffix.implicitWidth + 12
        horizontalAlignment: TextInput.AlignLeft
        text: control.shown()
        onEditingFinished: {
            const typed = parseFloat(text.replace(",", "."))
            if (isNaN(typed)) {
                text = control.shown()
                return
            }
            const next = Math.max(control.from, Math.min(control.to, typed))
            if (Math.abs(next - control.value) > 0.0001)
                control.edited(next)
            text = Qt.binding(() => control.shown())
        }
    }
    Text {
        id: suffix
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: control.unit
        color: Theme.faint
        font.pixelSize: 12
    }
}
