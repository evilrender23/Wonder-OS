import QtQuick
import QtQuick.Controls.Basic

/// «/» on an empty line: the blocks that can go there.
Popup {
    id: menu
    /// [{id, title, symbol, hint}]
    property var blocks: []
    property var shown: blocks
    signal chosen(string id)
    signal literal(string text)

    function show(query) {
        field.text = query ?? ""
        refill()
        open()
    }
    function refill() {
        const query = App.fold(field.text.trim())
        shown = blocks.filter(block => query === "" || App.fold(block.title).includes(query))
        list.currentIndex = 0
    }
    function accept() {
        if (list.currentIndex < 0 || list.currentIndex >= shown.length) {
            // Nothing is called that: it was text all along.
            const text = field.text
            close()
            literal(text)
            return
        }
        const id = shown[list.currentIndex].id
        close()
        chosen(id)
    }

    width: 250
    padding: 5
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: field.forceActiveFocus()
    background: Rectangle {
        radius: 10
        color: Theme.pill
        border.color: Theme.border
    }
    contentItem: Column {
        spacing: 4
        Field {
            id: field
            width: parent.width
            placeholderText: "Insertar…"
            onTextEdited: menu.refill()
            onAccepted: menu.accept()
            Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
            Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
            // Rubbing out the nothing that is there puts the menu away.
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Backspace && text === "") {
                    menu.close()
                    event.accepted = true
                }
            }
        }
        ListView {
            id: list
            width: parent.width
            height: Math.min(contentHeight, 252)
            clip: true
            model: menu.shown
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData
                width: list.width
                height: 28
                radius: 6
                color: list.currentIndex === index ? Theme.accent : "transparent"
                Sym {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.modelData.symbol
                    size: 15
                    color: list.currentIndex === row.index ? "#ffffff" : Theme.dim
                }
                Text {
                    x: 32
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.title
                    color: list.currentIndex === row.index ? "#ffffff" : Theme.text
                    font.pixelSize: 13
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.hint ?? ""
                    color: list.currentIndex === row.index ? "#d9ffffff" : Theme.faint
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onPositionChanged: list.currentIndex = row.index
                    onClicked: {
                        list.currentIndex = row.index
                        menu.accept()
                    }
                }
            }
        }
        Text {
            visible: list.count === 0
            leftPadding: 8
            bottomPadding: 6
            text: "Nada con ese nombre"
            color: Theme.dim
            font.pixelSize: 13
        }
    }
}
