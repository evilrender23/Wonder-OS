import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: menu
    padding: 5
    overlap: 4
    delegate: FMenuItem {}
    background: Rectangle {
        implicitWidth: 236
        radius: 9
        color: Theme.menu
        border.color: Theme.border
    }
}
