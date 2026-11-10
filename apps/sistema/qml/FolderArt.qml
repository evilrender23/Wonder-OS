import QtQuick
import QtQuick.Shapes

/// The desktop's folder, in the usual blue or in a tint: the same drawing and the same shades
/// the file manager paints (apps/rabbit, assets/icons/folder.svg).
Item {
    id: root

    /// "#rrggbb", or empty for the usual blue.
    property string tint: ""

    function shade(towards, amount) {
        const c = Qt.color(tint)
        return Qt.rgba(c.r + (towards - c.r) * amount, c.g + (towards - c.g) * amount, c.b + (towards - c.b) * amount, 1)
    }

    readonly property color backTop: tint === "" ? "#3da5f4" : shade(0, 0.05)
    readonly property color backBottom: tint === "" ? "#2587dd" : shade(0, 0.2)
    readonly property color frontTop: tint === "" ? "#86d3ff" : shade(1, 0.45)
    readonly property color frontBottom: tint === "" ? "#3fa4f1" : shade(1, 0.05)

    implicitWidth: 128
    implicitHeight: 128

    Shape {
        width: 128
        height: 128
        scale: Math.min(root.width, root.height) / 128
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 20; x2: 0; y2: 52
                GradientStop { position: 0; color: root.backTop }
                GradientStop { position: 1; color: root.backBottom }
            }
            PathSvg { path: "M8 30a10 10 0 0 1 10-10h27c3.6 0 7 1.6 9.3 4.4l4.5 5.6H110a10 10 0 0 1 10 10v12H8z" }
        }
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 38; x2: 0; y2: 108
                GradientStop { position: 0; color: root.frontTop }
                GradientStop { position: 1; color: root.frontBottom }
            }
            PathSvg { path: "M8 47a9 9 0 0 1 9-9h94a9 9 0 0 1 9 9v51a10 10 0 0 1-10 10H18A10 10 0 0 1 8 98z" }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: "#66ffffff"
            PathSvg { path: "M17 38h94a9 9 0 0 1 9 9v1.5a9 9 0 0 0-9-9H17a9 9 0 0 0-9 9V47a9 9 0 0 1 9-9z" }
        }
    }
}
