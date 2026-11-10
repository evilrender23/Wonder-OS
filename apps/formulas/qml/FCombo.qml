import QtQuick
import QtQuick.Controls.Basic

/// One of a list, behind a button. `colors`, when given, puts a dot before each.
ComboBox {
    id: combo
    property var colors: []
    implicitHeight: 28
    implicitWidth: 200
    font.pixelSize: 13
    leftPadding: 9
    rightPadding: 26
    contentItem: Row {
        spacing: 7
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: combo.colors.length > combo.currentIndex && combo.currentIndex >= 0
            width: 10
            height: 10
            radius: 5
            color: visible ? combo.colors[combo.currentIndex] : "transparent"
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - (combo.colors.length > 0 ? 17 : 0)
            text: combo.displayText
            color: Theme.text
            font: combo.font
            elide: Text.ElideRight
        }
    }
    indicator: Sym {
        x: combo.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: 13
        color: Theme.dim
    }
    background: Rectangle {
        radius: 7
        color: combo.hovered || combo.popup.visible ? Theme.sideSelection : Theme.field
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: ListView.view ? ListView.view.width : 0
        height: 26
        padding: 0
        highlighted: combo.highlightedIndex === index
        contentItem: Row {
            leftPadding: 8
            spacing: 7
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: combo.colors.length > option.index
                width: 10
                height: 10
                radius: 5
                color: visible ? combo.colors[option.index] : "transparent"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: option.modelData
                color: option.highlighted ? "#ffffff" : Theme.text
                font.pixelSize: 13
            }
        }
        background: Rectangle {
            radius: 5
            color: option.highlighted ? Theme.accent : "transparent"
        }
    }
    popup: Popup {
        y: combo.height + 3
        width: Math.max(combo.width, 180)
        padding: 5
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 300)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.delegateModel
            currentIndex: combo.highlightedIndex
        }
        background: Rectangle {
            radius: 9
            color: Theme.menu
            border.color: Theme.border
        }
    }
}
