import QtQuick
import QtQuick.Controls.Basic

/// One of a list: the table a form is for, what a report reads from…
Popup {
    id: dialog
    property string heading
    /// [{label, symbol, value}]
    property var options: []
    property var then: null

    function ask(title, list, callback) {
        heading = title
        options = list
        then = callback
        open()
    }

    modal: true
    anchors.centerIn: parent
    width: 340
    height: Math.min(parent.height - 80, 70 + Math.max(1, options.length) * 30 + 14)
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: "#40000000"
    }
    contentItem: Item {
        Text {
            id: title
            width: parent.width
            text: dialog.heading
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            anchors.top: title.bottom
            anchors.topMargin: 12
            visible: dialog.options.length === 0
            text: "No hay nada entre lo que elegir."
            color: Theme.dim
            font.pixelSize: 13
        }
        ListView {
            anchors.top: title.bottom
            anchors.topMargin: 10
            anchors.bottom: parent.bottom
            width: parent.width
            clip: true
            model: dialog.options
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: option
                required property var modelData
                width: ListView.view.width
                height: 30
                radius: 6
                color: optionArea.containsMouse ? Theme.accent : "transparent"
                Sym {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    name: option.modelData.symbol
                    size: 14
                    color: optionArea.containsMouse ? "#ffffff" : Theme.dim
                }
                Text {
                    x: 30
                    width: parent.width - 38
                    anchors.verticalCenter: parent.verticalCenter
                    text: option.modelData.label
                    color: optionArea.containsMouse ? "#ffffff" : Theme.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: optionArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        const callback = dialog.then
                        dialog.close()
                        if (callback)
                            callback(option.modelData.value)
                    }
                }
            }
        }
    }
}
