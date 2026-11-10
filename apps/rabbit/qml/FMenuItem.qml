import QtQuick
import QtQuick.Controls.Basic

MenuItem {
    id: item
    implicitHeight: visible ? 26 : 0
    leftPadding: 24
    rightPadding: 10
    font.pixelSize: 13
    contentItem: Text {
        text: item.text
        font: item.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        color: !item.enabled ? Theme.faint : (item.highlighted ? "white" : Theme.text)
    }
    indicator: Sym {
        x: 6
        anchors.verticalCenter: parent.verticalCenter
        size: 13
        name: "check"
        visible: item.checked
        color: item.highlighted ? "white" : Theme.text
    }
    arrow: Sym {
        x: item.width - width - 6
        anchors.verticalCenter: parent.verticalCenter
        size: 13
        name: "chevron-right"
        visible: item.subMenu !== null
        color: item.highlighted ? "white" : Theme.dim
    }
    background: Rectangle {
        radius: 5
        color: item.highlighted && item.enabled ? Theme.accent : "transparent"
    }
}
