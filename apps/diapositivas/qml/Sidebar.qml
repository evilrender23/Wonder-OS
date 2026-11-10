import QtQuick
import QtQuick.Controls.Basic

/// The slides one under another, or the whole deck as an outline to write in.
Rectangle {
    id: side
    property var ui
    readonly property var deck: App.deck
    color: Theme.sidebar

    Segmented {
        id: tabs
        x: 10
        y: 10
        options: [{key: "slides", label: "Diapositivas"}, {key: "outline", label: "Esquema"}]
        current: side.ui.sideTab
        onPicked: key => side.ui.sideTab = key
    }

    ListView {
        id: list
        visible: side.ui.sideTab === "slides"
        anchors.fill: parent
        anchors.topMargin: 48
        anchors.bottomMargin: 44
        clip: true
        model: side.deck.count
        currentIndex: side.deck.current
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        ScrollBar.vertical: ScrollBar { width: 8 }
        readonly property real thumbWidth: width - 52
        readonly property real thumbHeight: Math.round(thumbWidth * side.deck.height / side.deck.width)
        delegate: Item {
            id: row
            required property int index
            readonly property bool on: index === side.deck.current
            readonly property var info: { side.deck.revision; return side.deck.slide(index) }
            width: list.width
            height: list.thumbHeight + 14
            z: grab.drag.active ? 2 : 0
            Text {
                x: 8
                width: 20
                y: 8
                horizontalAlignment: Text.AlignRight
                text: row.index + 1
                color: row.on ? Theme.tint : Theme.dim
                font.pixelSize: 12
                font.weight: row.on ? Font.DemiBold : Font.Normal
            }
            Sym {
                visible: row.info.hidden === true
                x: 14
                y: 28
                name: "eye-off"
                size: 13
                color: Theme.dim
            }
            Sym {
                visible: (row.info.transition || "") !== ""
                x: 14
                y: row.info.hidden === true ? 46 : 28
                name: "sparkles"
                size: 13
                color: Theme.dim
            }
            Rectangle {
                id: thumb
                x: 36
                y: 5
                width: list.thumbWidth + 6
                height: list.thumbHeight + 6
                radius: 6
                color: "transparent"
                border.color: row.on ? Theme.tint : "transparent"
                border.width: 2
                opacity: row.info.hidden === true ? 0.45 : 1
                Rectangle {
                    anchors.fill: view
                    anchors.margins: -1
                    color: "transparent"
                    border.color: Theme.border
                    visible: !row.on
                }
                SlideView {
                    id: view
                    x: 3
                    y: 3
                    width: list.thumbWidth
                    height: list.thumbHeight
                    deck: side.deck
                    index: row.index
                    theme: side.ui.tryTheme
                }
            }
            MouseArea {
                id: grab
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property real from
                property bool moved: false
                onPressed: mouse => {
                    side.deck.current = row.index
                    from = mapToItem(list, mouse.x, mouse.y).y
                    moved = false
                    if (mouse.button === Qt.RightButton)
                        menu.popup()
                }
                onPositionChanged: mouse => {
                    if (!(pressedButtons & Qt.LeftButton))
                        return
                    const y = mapToItem(list, mouse.x, mouse.y).y
                    if (Math.abs(y - from) > 8)
                        moved = true
                    if (moved) {
                        const to = Math.max(0, Math.min(side.deck.count, Math.round((y + list.contentY) / row.height)))
                        drop.to = to
                        drop.visible = true
                    }
                }
                onReleased: {
                    if (moved && drop.visible) {
                        const to = drop.to > row.index ? drop.to - 1 : drop.to
                        if (to !== row.index)
                            side.deck.moveSlide(row.index, to)
                    }
                    drop.visible = false
                    moved = false
                }
                onDoubleClicked: side.ui.sorting = false
            }
        }
        Rectangle {
            id: drop
            property int to: 0
            visible: false
            x: 36
            y: to * (list.thumbHeight + 14) - list.contentY - 1
            width: list.thumbWidth + 6
            height: 3
            radius: 1.5
            color: Theme.tint
        }
    }

    // The outline: the deck follows what is written here.
    Item {
        visible: side.ui.sideTab === "outline"
        anchors.fill: parent
        anchors.topMargin: 48
        anchors.bottomMargin: 44
        Flickable {
            id: scroll
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 4
            contentHeight: outline.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { width: 8 }
            TextArea.flickable: TextArea {
                id: outline
                objectName: "outline"
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                color: Theme.text
                font.pixelSize: 13
                font.family: "monospace"
                selectionColor: Theme.accent
                selectedTextColor: "#ffffff"
                selectByMouse: true
                padding: 4
                rightPadding: 12
                background: null
                placeholderText: "# Título\n- Una viñeta\n  - Otra, más adentro\n> Una nota\n---\n# Siguiente"
                placeholderTextColor: Theme.faint
                property bool mine: false
                function refresh() {
                    if (!activeFocus) {
                        mine = true
                        text = side.deck.outline()
                        mine = false
                    }
                }
                Component.onCompleted: refresh()
                onActiveFocusChanged: refresh()
                onTextChanged: if (!mine) follow.restart()
                // A tab is a level deeper.
                Keys.onTabPressed: insert(cursorPosition, "  ")
            }
        }
        Connections {
            target: side.deck
            function onChanged() { outline.refresh() }
            function onFileChanged() { outline.refresh() }
        }
        Timer {
            id: follow
            interval: 350
            onTriggered: side.deck.setOutline(outline.text)
        }
    }
    function writeOutline(text) {
        outline.mine = true
        outline.text = text
        outline.mine = false
        side.deck.setOutline(text)
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 44
        color: "transparent"
        Rectangle { width: parent.width; height: 1; color: Theme.line }
        Row {
            visible: side.ui.sideTab === "slides"
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            TButton { symbol: "plus"; onClicked: side.ui.run("slideNew") }
            TButton { symbol: "copy-plus"; onClicked: side.ui.run("slideDuplicate") }
            TButton { symbol: "trash-2"; active: side.deck.count > 1; onClicked: side.ui.run("slideDelete") }
        }
        Text {
            visible: side.ui.sideTab === "slides"
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: side.deck.count === 1 ? "1 diapositiva" : side.deck.count + " diapositivas"
            color: Theme.dim
            font.pixelSize: 12
        }
        Text {
            visible: side.ui.sideTab === "outline"
            x: 12
            width: parent.width - 24
            anchors.verticalCenter: parent.verticalCenter
            text: "# título   - viñeta   > nota   --- otra"
            color: Theme.dim
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.line
    }

    FMenu {
        id: menu
        FMenuItem { text: "Nueva diapositiva"; onTriggered: side.ui.run("slideNew") }
        FMenuItem { text: "Duplicar"; onTriggered: side.ui.run("slideDuplicate") }
        FMenuItem { text: "Eliminar"; enabled: side.deck.count > 1; onTriggered: side.ui.run("slideDelete") }
        FSeparator {}
        FMenuItem { text: "Subir"; enabled: side.deck.current > 0; onTriggered: side.ui.run("slideUp") }
        FMenuItem { text: "Bajar"; enabled: side.deck.current < side.deck.count - 1; onTriggered: side.ui.run("slideDown") }
        FSeparator {}
        FMenuItem { text: "Omitir al presentar"; checkable: true; checked: side.deck.slide(side.deck.current).hidden === true; onTriggered: side.ui.run("slideHide") }
    }
}
