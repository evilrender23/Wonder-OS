import QtQuick
import QtQuick.Shapes
import ".."
import "../services"

// A filled line chart of 0–100 values (CPU history and the like).
Item {
    id: root

    property var values: []
    property color color: Theme.p.accent

    implicitHeight: 48

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: 1.5
            strokeColor: root.color
            fillColor: Qt.rgba(root.color.r, root.color.g, root.color.b, 0.12)
            startX: 0
            startY: root.height
            PathPolyline {
                path: {
                    const v = root.values
                    const n = Math.max(v.length, 2)
                    const pts = [Qt.point(0, root.height)]
                    for (let i = 0; i < v.length; i++)
                        pts.push(Qt.point(i / (n - 1) * root.width, root.height - Math.max(0, Math.min(100, v[i])) / 100 * (root.height - 2) - 1))
                    pts.push(Qt.point(v.length > 0 ? (v.length - 1) / (n - 1) * root.width : 0, root.height))
                    return pts
                }
            }
        }
    }
}
