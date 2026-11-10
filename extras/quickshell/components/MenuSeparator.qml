import QtQuick
import QtQuick.Layouts
import ".."
import "../services"

// The hairline between groups of a macOS-style menu (see MenuRow).
Rectangle {
    Layout.fillWidth: true
    Layout.leftMargin: 10
    Layout.rightMargin: 10
    Layout.topMargin: 5
    Layout.bottomMargin: 5
    implicitHeight: 1
    color: Theme.p.stroke2
}
