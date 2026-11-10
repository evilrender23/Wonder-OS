import QtQuick

Item {
    id: root

    property color color: Theme.dim
    property int size: 16

    width: size
    height: size

    Canvas {
        id: arc
        anchors.fill: parent
        property color color: root.color
        onColorChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.lineWidth = 2
            ctx.lineCap = "round"
            ctx.strokeStyle = color
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, width / 2 - 2, 0, Math.PI * 1.4)
            ctx.stroke()
        }
        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: root.visible
        }
    }
}
